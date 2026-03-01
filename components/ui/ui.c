#include "ui.h"
#include "ui_page.h"
#include "ui_types.h"
#include "ui_render.h"
#include "board.h"
#include "storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* UI_TAG = "ui_manager";

/* ================== UI 互斥锁 ================== */
// 保护所有 UI 状态（state, messages, indices, page vars）
// 避免 app_task(ui_on_key) 与 gui_task(ui_tick) 之间的竞态
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

#define STANDBY_TIMEOUT_MS 30000
#define DEFAULT_BRIGHTNESS 100

/* ================== 外部页面引用 ================== */
extern const ui_page_t page_main;
extern const ui_page_t page_list;
extern const ui_page_t page_message;
extern const ui_page_t page_settings;

static const ui_page_t* s_pages[] = {
    [UI_STATE_MAIN] = &page_main,
    [UI_STATE_MESSAGE_LIST] = &page_list,
    [UI_STATE_MESSAGE_READ] = &page_message,
    [UI_STATE_SETTINGS] = &page_settings,
};

/* ================== 内部状态定义 ================== */
typedef struct {
    ui_state_enum_t state;
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
    bool flashlight_on;      // 手电筒状态
    uint8_t brightness;      // OLED 亮度 (10-100%)
} ui_context_t;

static ui_context_t s_ui;

/* ================== 数据访问接口 ================== */
int ui_get_message_count(void) { return s_ui.message_count; }
int ui_get_current_message_idx(void) { return s_ui.current_msg_idx; }
void ui_set_current_message_idx(int idx) {
    s_ui.current_msg_idx = idx;
    // persist current index
    storage_save_messages(s_ui.messages, s_ui.message_count, s_ui.current_msg_idx);
}

int ui_get_unread_count(void) {
    int unread = 0;
    for(int i=0; i<s_ui.message_count; i++) {
        if(!s_ui.messages[i].is_read) unread++;
    }
    return unread;
}

ui_message_t* ui_get_message_at(int idx) {
    if (idx < 0 || idx >= s_ui.message_count) return NULL;
    return &s_ui.messages[idx];
}

/* ================== 辅助函数 ================== */
static void ui_update_activity(void) {
    s_ui.last_activity_time = board_time_ms();
}

void ui_change_page(ui_state_enum_t new_state) {
    ESP_LOGD(UI_TAG, "Changing page from %d to %d", s_ui.state, new_state);

    if (new_state == s_ui.state) {
        ESP_LOGD(UI_TAG, "Page change ignored - same state");
        return;
    }

    // Validate transitions: do not enter message-related pages when there are no messages
    if ((new_state == UI_STATE_MESSAGE_LIST || new_state == UI_STATE_MESSAGE_READ) && s_ui.message_count == 0) {
        ESP_LOGW(UI_TAG, "Attempt to enter message page but no messages exist, redirecting to MAIN");
        new_state = UI_STATE_MAIN;
    }

    // Ensure current index is within valid range when entering message pages
    if (new_state == UI_STATE_MESSAGE_LIST || new_state == UI_STATE_MESSAGE_READ) {
        if (s_ui.current_msg_idx < 0) s_ui.current_msg_idx = 0;
        if (s_ui.current_msg_idx >= s_ui.message_count && s_ui.message_count > 0) {
            // default to the most recent message for better UX
            s_ui.current_msg_idx = s_ui.message_count - 1;
        }
    }

    // 调用旧页面的 exit
    if (s_ui.state != UI_STATE_STANDBY && s_pages[s_ui.state] && s_pages[s_ui.state]->on_exit) {
        ESP_LOGD(UI_TAG, "Calling exit handler for state %d", s_ui.state);
        s_pages[s_ui.state]->on_exit();
    }

    ui_state_enum_t old_state = s_ui.state;
    s_ui.state = new_state;

    // 调用新页面的 enter
    if (new_state != UI_STATE_STANDBY && s_pages[new_state] && s_pages[new_state]->on_enter) {
        ESP_LOGD(UI_TAG, "Calling enter handler for state %d", new_state);
        s_pages[new_state]->on_enter();
    }

    ESP_LOGD(UI_TAG, "Page change completed: %d -> %d", old_state, new_state);
}

/* ================== 核心接口实现 ================== */
void ui_init(void) {
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.brightness = DEFAULT_BRIGHTNESS;
    s_ui.flashlight_on = false;

    // 创建 UI 互斥锁（保护 UI 状态免受多任务竞态）
    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        ESP_LOGE(UI_TAG, "Failed to create UI mutex!");
    }

    // initialize NVS storage and load persisted messages
    if (storage_init() == ESP_OK) {
        int loaded_count = 0;
        int loaded_idx = 0;
        if (storage_load_messages(s_ui.messages, &loaded_count, &loaded_idx) == ESP_OK) {
            s_ui.message_count = loaded_count;
            s_ui.current_msg_idx = loaded_idx;
            ESP_LOGI(UI_TAG, "Loaded %d messages from storage, current idx=%d", loaded_count, loaded_idx);
        }
        // 加载保存的亮度设置
        uint8_t saved_brightness = 0;
        if (storage_load_brightness(&saved_brightness) == ESP_OK) {
            s_ui.brightness = saved_brightness;
            board_display_set_contrast((uint8_t)((saved_brightness * 255) / 100));
            ESP_LOGI(UI_TAG, "Loaded brightness: %d%%", saved_brightness);
        }
    } else {
        ESP_LOGW(UI_TAG, "storage_init failed");
    }
    s_ui.state = UI_STATE_MAIN;
    ui_update_activity();
    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_enter) {
        s_pages[s_ui.state]->on_enter();
    }
    ESP_LOGI(UI_TAG, "UI Manager initialized");
}

void ui_tick(void) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_tick: failed to acquire lock, skip frame");
        return;
    }

    if (s_ui.state != UI_STATE_STANDBY) {
        if (board_time_ms() - s_ui.last_activity_time > STANDBY_TIMEOUT_MS) {
            ESP_LOGD(UI_TAG, "Activity timeout, entering standby");
            ui_enter_standby();
            ui_unlock();
            return;
        }

        if (s_pages[s_ui.state] && s_pages[s_ui.state]->tick) {
            s_pages[s_ui.state]->tick();
        }
    } else {
        // 在待机状态下，周期性渲染屏保并等待按键唤醒
        ui_render_standby();
        ESP_LOGD(UI_TAG, "In standby state, rendered standby frame");
    }

    ui_unlock();
}

void ui_on_key(board_key_t key) {
    ESP_LOGI(UI_TAG, "UI received key: %d, current state: %d", key, s_ui.state);

    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_on_key: failed to acquire lock, drop key %d", key);
        return;
    }

    ui_update_activity();

    if (s_ui.state == UI_STATE_STANDBY) {
        ESP_LOGI(UI_TAG, "Waking up from standby with key %d", key);
        ui_wake_up();
        // 唤醒后，如果按键是 ENTER 或 DOWN，继续处理
        if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN || key == BOARD_KEY_UP) {
            ESP_LOGI(UI_TAG, "Processing key %d after wake up", key);
            // 显示已初始化，直接处理按键
            if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
                s_pages[s_ui.state]->on_key(key);
                ESP_LOGI(UI_TAG, "Delegated key %d to page handler for state %d", key, s_ui.state);
            }
        }
        ui_unlock();
        return;
    }

    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
        ESP_LOGI(UI_TAG, "Passing key %d to page handler for state %d", key, s_ui.state);
        s_pages[s_ui.state]->on_key(key);
    } else {
        ESP_LOGW(UI_TAG, "No key handler for state %d", s_ui.state);
    }

    ui_unlock();
}

void ui_show_message(const char* sender, const char* text) {
    // 调用带时间戳的版本，使用当前时间
    ui_show_message_with_timestamp(sender, text, (uint32_t)time(NULL));
}

void ui_show_message_with_timestamp(const char* sender, const char* text, uint32_t timestamp) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_show_message_with_timestamp: failed to acquire lock, message dropped");
        return;
    }

    if (s_ui.message_count >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            s_ui.messages[i] = s_ui.messages[i + 1];
        }
        s_ui.message_count = MAX_MESSAGES - 1;
    }

    ui_message_t* msg = &s_ui.messages[s_ui.message_count++];
    strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    msg->sender[sizeof(msg->sender)-1] = '\0';
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text)-1] = '\0';
    // 使用传入的时间戳
    msg->timestamp = timestamp;
    msg->is_read = false;

    ESP_LOGI(UI_TAG, "显示消息 - 发送者: %s, 时间戳: %u", sender, timestamp);

    ui_wake_up();
    s_ui.current_msg_idx = s_ui.message_count - 1;
    ui_change_page(UI_STATE_MESSAGE_READ);
    board_notify();
    // 来信提醒 LED 闪烁 + 震动
    board_leds_double_flash();
    board_vibrate_double();
    // persist after adding
    storage_save_messages(s_ui.messages, s_ui.message_count, s_ui.current_msg_idx);

    ui_unlock();
}

void ui_enter_standby(void) {
    if (s_ui.state != UI_STATE_STANDBY) {
        // 使用统一的页面切换流程以触发当前页面的 exit handler
        ui_change_page(UI_STATE_STANDBY);
        // 渲染待机屏保
        ui_render_standby();
        // 进入待机时，只有手电筒未开启才关闭 LED
        if (!s_ui.flashlight_on) {
            board_leds_off();
        }
        ESP_LOGI(UI_TAG, "Entered standby");
    }
}

bool ui_is_in_standby(void) {
    return s_ui.state == UI_STATE_STANDBY;
}

void ui_wake_up(void) {
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_change_page(UI_STATE_MAIN);
        ui_update_activity();
        ESP_LOGI(UI_TAG, "Woke up");
    }
}

/* ================== 消息删除功能 ================== */
void ui_delete_current_message(void) {
    if (s_ui.message_count <= 0) return;

    int idx = s_ui.current_msg_idx;
    if (idx < 0 || idx >= s_ui.message_count) return;

    // 移动后面的消息
    for (int i = idx; i < s_ui.message_count - 1; i++) {
        s_ui.messages[i] = s_ui.messages[i + 1];
    }
    s_ui.message_count--;

    // 调整当前索引
    if (s_ui.current_msg_idx >= s_ui.message_count && s_ui.message_count > 0) {
        s_ui.current_msg_idx = s_ui.message_count - 1;
    }

    // 持久化
    storage_save_messages(s_ui.messages, s_ui.message_count, s_ui.current_msg_idx);

    ESP_LOGI(UI_TAG, "Deleted message at idx %d, remaining: %d", idx, s_ui.message_count);
}

/* ================== 手电筒功能 ================== */
bool ui_is_flashlight_on(void) {
    return s_ui.flashlight_on;
}

void ui_toggle_flashlight(void) {
    s_ui.flashlight_on = !s_ui.flashlight_on;

    if (s_ui.flashlight_on) {
        // 点亮所有 LED 作为手电筒
        board_leds_t leds = { .led1 = 255, .led2 = 255, .led3 = 255 };
        board_leds_set(leds);
        ESP_LOGI(UI_TAG, "Flashlight ON");
    } else {
        ESP_LOGI(UI_TAG, "Flashlight OFF");
    }
}

/* ================== 亮度控制 ================== */
uint8_t ui_get_brightness(void) {
    return s_ui.brightness;
}

void ui_set_brightness(uint8_t level) {
    if (level < 10) level = 10;
    if (level > 100) level = 100;
    s_ui.brightness = level;

    // 应用亮度到显示器
    board_display_set_contrast((uint8_t)((level * 255) / 100));

    // 保存到存储
    storage_save_brightness(level);

    ESP_LOGI(UI_TAG, "Brightness set to %d%%", level);
}
