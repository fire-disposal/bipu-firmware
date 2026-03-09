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
    // 增加超时时间到 1 秒，减少误报
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE("ui", "Failed to acquire UI lock!");
        return false;
    }
    return true;
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

/* NVS 写入频率限制：两次保存之间最小间隔 (ms) */
#define NVS_SAVE_INTERVAL_MS  2000
static uint32_t s_last_msg_save_time = 0;
static uint32_t s_last_brightness_save_time = 0;

/* BLE 重连后需要同步的状态 */
static bool s_pending_ble_resync = false;

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

    // 快照消息数组（标记待保存，但不立即写入）
    memcpy(s_save_snap, s_ui_data.messages, sizeof(storage_message_t) * s_ui_data.message_count);
    s_save_count = s_ui_data.message_count;
    s_save_idx = s_ui_data.current_msg_idx;
    s_deferred_msg_save = true;

    ui_unlock();

    // NVS 持久化改为由 app_loop 定期批量保存（见 ui_flush_pending_saves）

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

/* ================== 延迟 NVS 持久化（带频率限制） ================== */
void ui_flush_pending_saves(void)
{
    uint32_t now = board_time_ms();
    
    // 消息保存（频率限制：2 秒间隔）
    if (s_deferred_msg_save) {
        if (now - s_last_msg_save_time >= NVS_SAVE_INTERVAL_MS) {
            s_deferred_msg_save = false;
            s_last_msg_save_time = now;
            storage_save_messages(s_save_snap, s_save_count, s_save_idx);
            ESP_LOGD("ui", "NVS save: %d messages", s_save_count);
        }
    }
    
    // 亮度保存（频率限制：2 秒间隔）
    if (s_deferred_brightness_save) {
        if (now - s_last_brightness_save_time >= NVS_SAVE_INTERVAL_MS) {
            s_deferred_brightness_save = false;
            s_last_brightness_save_time = now;
            storage_save_brightness(s_save_brightness);
            ESP_LOGD("ui", "NVS save: brightness=%d", s_save_brightness);
        }
    }
}

/* 强制立即保存所有待存数据（页面切换/退出时调用） */
void ui_flush_pending_saves_force(void)
{
    if (s_deferred_msg_save) {
        s_deferred_msg_save = false;
        s_last_msg_save_time = board_time_ms();
        storage_save_messages(s_save_snap, s_save_count, s_save_idx);
    }
    if (s_deferred_brightness_save) {
        s_deferred_brightness_save = false;
        s_last_brightness_save_time = board_time_ms();
        storage_save_brightness(s_save_brightness);
    }
}

/* 标记需要 NVS 保存（供外部调用） */
void ui_request_nvs_save(void)
{
    if (!ui_lock()) return;
    
    memcpy(s_save_snap, s_ui_data.messages, sizeof(storage_message_t) * s_ui_data.message_count);
    s_save_count = s_ui_data.message_count;
    s_save_idx = s_ui_data.current_msg_idx;
    s_deferred_msg_save = true;
    
    ui_unlock();
}

/* 查询是否有待保存的 NVS 数据 */
bool ui_has_pending_saves(void)
{
    return s_deferred_msg_save || s_deferred_brightness_save;
}

/* BLE 重连后恢复状态 */
void ui_on_ble_reconnected(void)
{
    ESP_LOGI("ui", "BLE reconnected, resyncing state");
    
    /* 强制保存当前状态（防止断开期间数据丢失） */
    ui_flush_pending_saves_force();
    
    /* 重置重连标记 */
    s_pending_ble_resync = false;
    
    /* 刷新显示（可选：显示连接恢复提示） */
    ui_request_redraw();
}

/* ================== 导航接口实现 ================== */

void ui_go_back_page(void)
{
    ui_state_go_back();
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

/* ================== 状态机导航包装 ================== */
void ui_navigate_to_page(ui_page_base_t* page, void* params)
{
    ui_state_machine_navigate(page, params);
}

/* ================== 重绘接口包装（供 app 层使用） ================== */
void ui_request_redraw(void)
{
    ui_task_request_redraw();
}
