#include "ui.h"
#include "ui_page.h"
#include "ui_types.h"
#include "ui_render.h"
#include "board.h"
#include "esp_log.h"
#include <string.h>

static const char* UI_TAG = "ui_manager";

#define STANDBY_TIMEOUT_MS 30000

/* ================== 外部页面引用 ================== */
extern const ui_page_t page_main;
extern const ui_page_t page_message;

static const ui_page_t* s_pages[] = {
    [UI_STATE_MAIN] = &page_main,
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
void ui_set_current_message_idx(int idx) { s_ui.current_msg_idx = idx; }

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
    if (new_state == s_ui.state) return;
    
    // 调用旧页面的 exit
    if (s_ui.state != UI_STATE_STANDBY && s_pages[s_ui.state] && s_pages[s_ui.state]->on_exit) {
        s_pages[s_ui.state]->on_exit();
    }
    
    s_ui.state = new_state;
    
    // 调用新页面的 enter
    if (new_state != UI_STATE_STANDBY && s_pages[new_state] && s_pages[new_state]->on_enter) {
        s_pages[new_state]->on_enter();
    }
}

/* ================== 核心接口实现 ================== */
void ui_init(void) {
    memset(&s_ui, 0, sizeof(s_ui));
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
            ui_enter_standby();
            return;
        }
        
        if (s_pages[s_ui.state] && s_pages[s_ui.state]->tick) {
            s_pages[s_ui.state]->tick();
        }
    }
}

void ui_on_key(board_key_t key) {
    ui_update_activity();
    
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_wake_up();
        return;
    }
    
    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
        s_pages[s_ui.state]->on_key(key);
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
    msg->timestamp = board_time_ms();
    msg->is_read = false;
    
    ui_wake_up();
    s_ui.current_msg_idx = s_ui.message_count - 1;
    ui_change_page(UI_STATE_MESSAGE_READ);
    board_notify();
}

void ui_enter_standby(void) {
    if (s_ui.state != UI_STATE_STANDBY) {
        s_ui.state = UI_STATE_STANDBY;
        ui_render_standby();
        board_rgb_off();
        ESP_LOGI(UI_TAG, "Entered standby");
    }
}

void ui_wake_up(void) {
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_change_page(UI_STATE_MAIN);
        ui_update_activity();
        board_vibrate_on(50);
        ESP_LOGI(UI_TAG, "Woke up");
    }
}

