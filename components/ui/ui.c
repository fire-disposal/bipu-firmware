#include "ui.h"
#include "ui_page.h"
#include "ui_types.h"
#include "ui_render.h"
#include "board.h"
#include "storage.h"
#include "esp_log.h"
#include <string.h>

static const char* UI_TAG = "ui_manager";

#define STANDBY_TIMEOUT_MS 30000

/* ================== 外部页面引用 ================== */
extern const ui_page_t page_main;
extern const ui_page_t page_list;
extern const ui_page_t page_message;

static const ui_page_t* s_pages[] = {
    [UI_STATE_MAIN] = &page_main,
    [UI_STATE_MESSAGE_LIST] = &page_list,
    [UI_STATE_MESSAGE_READ] = &page_message,
};

/* ================== 内部状态定义 ================== */
typedef struct {
    ui_state_enum_t state;
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
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
    // initialize NVS storage and load persisted messages
    if (storage_init() == ESP_OK) {
        int loaded_count = 0;
        int loaded_idx = 0;
        if (storage_load_messages(s_ui.messages, &loaded_count, &loaded_idx) == ESP_OK) {
            s_ui.message_count = loaded_count;
            s_ui.current_msg_idx = loaded_idx;
            ESP_LOGI(UI_TAG, "Loaded %d messages from storage, current idx=%d", loaded_count, loaded_idx);
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
    if (s_ui.state != UI_STATE_STANDBY) {
        if (board_time_ms() - s_ui.last_activity_time > STANDBY_TIMEOUT_MS) {
            ESP_LOGD(UI_TAG, "Activity timeout, entering standby");
            ui_enter_standby();
            return;
        }
        
        if (s_pages[s_ui.state] && s_pages[s_ui.state]->tick) {
            s_pages[s_ui.state]->tick();
        }
    } else {
        // 在待机状态下，我们仍然需要检查按键来唤醒
        ESP_LOGD(UI_TAG, "In standby state, waiting for key press");
    }
}

void ui_on_key(board_key_t key) {
    ESP_LOGI(UI_TAG, "UI received key: %d, current state: %d", key, s_ui.state);
    ui_update_activity();

    if (s_ui.state == UI_STATE_STANDBY) {
        ESP_LOGI(UI_TAG, "Waking up from standby with key %d", key);
        ui_wake_up();
        // 唤醒后，如果按键是ENTER或DOWN，继续处理
        if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN || key == BOARD_KEY_UP) {
            ESP_LOGI(UI_TAG, "Processing key %d after wake up", key);
            // 显示已初始化，直接处理按键
            if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
                s_pages[s_ui.state]->on_key(key);
                ESP_LOGI(UI_TAG, "Delegated key %d to page handler for state %d", key, s_ui.state);
            }
        }
        return;
    }

    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
        ESP_LOGI(UI_TAG, "Passing key %d to page handler for state %d", key, s_ui.state);
        s_pages[s_ui.state]->on_key(key);
    } else {
        ESP_LOGW(UI_TAG, "No key handler for state %d", s_ui.state);
    }
}

void ui_show_message(const char* sender, const char* text) {
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
    // 使用 wall-clock 时间记录接收时间，以便在消息列表中显示
    msg->timestamp = (uint32_t)time(NULL);
    msg->is_read = false;
    
    ui_wake_up();
    s_ui.current_msg_idx = s_ui.message_count - 1;
    ui_change_page(UI_STATE_MESSAGE_READ);
    board_notify();
    // persist after adding
    storage_save_messages(s_ui.messages, s_ui.message_count, s_ui.current_msg_idx);
}

void ui_enter_standby(void) {
    if (s_ui.state != UI_STATE_STANDBY) {
        s_ui.state = UI_STATE_STANDBY;
        ui_render_standby();
        board_leds_off();
        ESP_LOGI(UI_TAG, "Entered standby");
    }
}

void ui_wake_up(void) {
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_change_page(UI_STATE_MAIN);
        ui_update_activity();
        ESP_LOGI(UI_TAG, "Woke up");
    }
}

