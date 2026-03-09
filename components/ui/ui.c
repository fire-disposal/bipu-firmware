#include "ui.h"
#include "ui_types.h"
#include "ui_render.h"
#include "ui_task.h"
#include "ui_state_machine.h"
#include "board.h"
#include "storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

static const char* UI_TAG = "ui_manager";

/* ================== UI 互斥锁 ================== */
static SemaphoreHandle_t s_ui_mutex = NULL;

static inline bool ui_lock(void) {
    if (s_ui_mutex == NULL) return true;
    return xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(200)) == pdTRUE;
}

static inline void ui_unlock(void) {
    if (s_ui_mutex != NULL) {
        xSemaphoreGive(s_ui_mutex);
    }
}

/* ================== 延迟 NVS 保存状态 ================== */
static bool s_deferred_msg_save = false;
static bool s_deferred_brightness_save = false;
static storage_message_t s_save_snap[MAX_MESSAGES];
static int  s_save_count;
static int  s_save_idx;
static uint8_t s_save_brightness;

/* ================== Toast 状态 ================== */
#define TOAST_MSG_MAX 64
static char     s_toast_msg[TOAST_MSG_MAX];
static bool     s_toast_visible   = false;
static uint32_t s_toast_expire_ms = 0;

/* ================== UI 数据状态 ================== */
typedef struct {
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
    bool flashlight_on;
    uint8_t brightness;
} ui_data_t;

static ui_data_t s_ui_data = {0};

/* ================== 数据访问接口 ================== */
int ui_get_message_count(void) { return s_ui_data.message_count; }
int ui_get_current_message_idx(void) { return s_ui_data.current_msg_idx; }

void ui_set_current_message_idx(int idx) {
    if (s_ui_data.current_msg_idx != idx) {
        s_ui_data.current_msg_idx = idx;
        ui_task_request_redraw();
    }
}

int ui_get_unread_count(void) {
    int unread = 0;
    for(int i=0; i<s_ui_data.message_count; i++) {
        if(!s_ui_data.messages[i].is_read) unread++;
    }
    return unread;
}

uint32_t ui_get_last_activity_time(void) {
    return s_ui_data.last_activity_time;
}

ui_message_t* ui_get_message_at(int idx) {
    if (idx < 0 || idx >= s_ui_data.message_count) return NULL;
    return &s_ui_data.messages[idx];
}

static void ui_update_activity(void) {
    s_ui_data.last_activity_time = board_time_ms();
}

/* ================== GUI 任务重绘回调 ================== */
static void ui_redraw_callback_wrapper(void) {
    ui_task_request_redraw();
}

/* ================== Toast 预刷新钩子 ================== */
static void toast_pre_flush_cb(void) {
    if (s_toast_visible) {
        ui_render_toast(s_toast_msg);
    }
}

/* ================== 核心接口实现 ================== */
void ui_init(void) {
    memset(&s_ui_data, 0, sizeof(s_ui_data));
    s_ui_data.brightness = 100;
    s_ui_data.flashlight_on = false;

    // 创建 UI 互斥锁
    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        ESP_LOGE(UI_TAG, "Failed to create UI mutex!");
    }

    // 初始化状态机
    ui_state_machine_init();
    
    // 启动 GUI 任务
    ui_task_start();
    ui_task_set_redraw_callback(ui_redraw_callback_wrapper);

    // 加载 NVS 数据
    if (storage_init() == ESP_OK) {
        int loaded_count = 0;
        int loaded_idx = 0;
        if (storage_load_messages(s_ui_data.messages, &loaded_count, &loaded_idx) == ESP_OK) {
            s_ui_data.message_count = loaded_count;
            s_ui_data.current_msg_idx = loaded_idx;
            ESP_LOGI(UI_TAG, "Loaded %d messages from storage", loaded_count);
        }
        
        uint8_t saved_brightness = 0;
        if (storage_load_brightness(&saved_brightness) == ESP_OK) {
            s_ui_data.brightness = saved_brightness;
            board_display_set_contrast((uint8_t)((saved_brightness * 255) / 100));
        }
    }
    
    // 注册 Toast 钩子
    board_display_set_pre_flush_cb(toast_pre_flush_cb);
    
    // 导航到主页
    ui_navigate_to_page(ui_get_main_page(), NULL);
    
    ESP_LOGI(UI_TAG, "UI initialized (new architecture)");
}

uint32_t ui_tick(void) {
    if (!ui_lock()) {
        return 100;
    }

    uint32_t next_sleep_ms = 1000;
    ui_page_base_t* current_page = ui_get_current_page();
    
    // Toast 超时检查
    if (s_toast_visible && s_toast_expire_ms > 0) {
        if (board_time_ms() >= s_toast_expire_ms) {
            s_toast_visible = false;
        } else {
            uint32_t remain = s_toast_expire_ms - board_time_ms();
            if (remain < next_sleep_ms) next_sleep_ms = remain;
        }
    }
    
    // 页面更新
    if (current_page && current_page->update && current_page->update_interval > 0) {
        current_page->update(current_page, current_page->update_interval);
    }

    ui_unlock();

    // 渲染（无锁）
    if (current_page && current_page->needs_render && current_page->render) {
        current_page->render(current_page);
        current_page->needs_render = false;
    }

    return next_sleep_ms > 0 ? next_sleep_ms : 1000;
}

void ui_on_key(board_key_t key) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_on_key: failed to acquire lock");
        return;
    }

    ui_update_activity();

    // Toast 拦截
    if (s_toast_visible) {
        s_toast_visible = false;
        ui_unlock();
        ui_task_request_redraw();
        return;
    }

    // 获取当前页面并处理按键
    ui_page_base_t* current_page = ui_get_current_page();
    if (current_page && current_page->on_key) {
        current_page->on_key(current_page, key);
    }

    ui_unlock();
}

/* ================== 消息接口 ================== */
void ui_show_message(const char* sender, const char* text) {
    ui_show_message_with_timestamp(sender, text, (uint32_t)time(NULL));
}

void ui_show_message_with_timestamp(const char* sender, const char* text, uint32_t timestamp) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_show_message: failed to acquire lock");
        return;
    }

    if (s_ui_data.message_count >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            s_ui_data.messages[i] = s_ui_data.messages[i + 1];
        }
        s_ui_data.message_count = MAX_MESSAGES - 1;
    }

    ui_message_t* msg = &s_ui_data.messages[s_ui_data.message_count++];
    strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    msg->sender[sizeof(msg->sender)-1] = '\0';
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text)-1] = '\0';
    msg->timestamp = timestamp;
    msg->is_read = false;

    s_ui_data.current_msg_idx = s_ui_data.message_count - 1;

    // 快照消息数组
    memcpy(s_save_snap, s_ui_data.messages, sizeof(storage_message_t) * s_ui_data.message_count);
    s_save_count = s_ui_data.message_count;
    s_save_idx = s_ui_data.current_msg_idx;
    s_deferred_msg_save = true;

    ui_unlock();

    // NVS 持久化（锁外）
    storage_save_messages(s_save_snap, s_save_count, s_save_idx);

    // 导航到消息页面并通知硬件
    ui_navigate_to_page(ui_get_message_page(), NULL);
    board_notify();
    board_leds_double_flash();
    board_vibrate_double();
}

void ui_delete_current_message(void) {
    if (!ui_lock()) return;
    
    if (s_ui_data.message_count <= 0) {
        ui_unlock();
        return;
    }

    int idx = s_ui_data.current_msg_idx;
    if (idx < 0 || idx >= s_ui_data.message_count) {
        ui_unlock();
        return;
    }

    for (int i = idx; i < s_ui_data.message_count - 1; i++) {
        s_ui_data.messages[i] = s_ui_data.messages[i + 1];
    }
    s_ui_data.message_count--;

    if (s_ui_data.current_msg_idx >= s_ui_data.message_count && s_ui_data.message_count > 0) {
        s_ui_data.current_msg_idx = s_ui_data.message_count - 1;
    }

    memcpy(s_save_snap, s_ui_data.messages, sizeof(storage_message_t) * s_ui_data.message_count);
    s_save_count = s_ui_data.message_count;
    s_save_idx = s_ui_data.current_msg_idx;
    s_deferred_msg_save = true;

    ui_unlock();
}

/* ================== 手电筒功能 ================== */
bool ui_is_flashlight_on(void) {
    return s_ui_data.flashlight_on;
}

void ui_toggle_flashlight(void) {
    s_ui_data.flashlight_on = !s_ui_data.flashlight_on;

    if (s_ui_data.flashlight_on) {
        board_leds_t leds = { .led1 = 255, .led2 = 255, .led3 = 255 };
        board_leds_set(leds);
    } else {
        board_leds_off();
    }
    
    ui_task_request_redraw();
}

/* ================== 亮度控制 ================== */
uint8_t ui_get_brightness(void) {
    return s_ui_data.brightness;
}

void ui_set_brightness(uint8_t level) {
    if (level < 10) level = 10;
    if (level > 100) level = 100;
    s_ui_data.brightness = level;

    board_display_set_contrast((uint8_t)((level * 255) / 100));

    s_save_brightness = level;
    s_deferred_brightness_save = true;
    
    ui_task_request_redraw();
}

/* ================== 系统控制 ================== */
void ui_system_restart(void) {
    board_display_set_contrast(0);
    board_execute_cleanup();
    board_system_restart();
}

/* ================== 延迟 NVS 持久化 ================== */
void ui_flush_pending_saves(void) {
    if (s_deferred_msg_save) {
        s_deferred_msg_save = false;
        storage_save_messages(s_save_snap, s_save_count, s_save_idx);
    }
    if (s_deferred_brightness_save) {
        s_deferred_brightness_save = false;
        storage_save_brightness(s_save_brightness);
    }
}

/* ================== Toast API ================== */
void ui_show_toast(const char *msg, uint32_t auto_dismiss_ms) {
    if (!msg) return;
    strncpy(s_toast_msg, msg, TOAST_MSG_MAX - 1);
    s_toast_msg[TOAST_MSG_MAX - 1] = '\0';
    s_toast_visible = true;
    s_toast_expire_ms = (auto_dismiss_ms > 0) ? (board_time_ms() + auto_dismiss_ms) : 0;
    ui_task_request_redraw();
}

bool ui_toast_is_visible(void) {
    return s_toast_visible;
}

void ui_toast_dismiss(void) {
    if (s_toast_visible) {
        s_toast_visible = false;
        ui_task_request_redraw();
    }
}

/* ================== 向后兼容接口 ================== */
void ui_change_page(ui_state_enum_t new_state) {
    // 临时兼容层，最终将移除
    if (new_state == UI_STATE_MAIN) {
        ui_navigate_to_page(ui_get_main_page(), NULL);
    } else if (new_state == UI_STATE_MESSAGE_LIST) {
        ui_navigate_to_page(ui_get_list_page(), NULL);
    } else if (new_state == UI_STATE_MESSAGE_READ) {
        ui_navigate_to_page(ui_get_message_page(), NULL);
    } else if (new_state == UI_STATE_SETTINGS) {
        ui_navigate_to_page(ui_get_settings_page(), NULL);
    }
}

void ui_enter_standby(void) {
    board_display_set_contrast(0);
    board_leds_off();
}

void ui_wake_up(void) {
    ui_navigate_to_page(ui_get_main_page(), NULL);
    ui_update_activity();
}

bool ui_is_in_standby(void) {
    return false;  // 新架构不使用 standby 状态
}

/* ================== 状态机导航包装 ================== */
void ui_navigate_to_page(ui_page_base_t* page, void* params) {
    ui_state_machine_navigate(page, params);
}
