#include "ui.h"
#include "ui_types.h"
#include "ui_render.h"
#include "board.h"
#include "ble_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* UI_TAG = "ui";

#define STANDBY_TIMEOUT_MS 30000

/* ================== 内部状态定义 ================== */

typedef struct {
    ui_state_enum_t state;
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
} ui_context_t;

static ui_context_t s_ui;

/* ================== 辅助函数 ================== */

static void ui_update_activity(void)
{
    s_ui.last_activity_time = board_time_ms();
}

static void ui_add_message_internal(const char* sender, const char* text)
{
    if (s_ui.message_count >= MAX_MESSAGES) {
        // 移除最旧的消息（简单的内存移动）
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            s_ui.messages[i] = s_ui.messages[i + 1];
        }
        s_ui.message_count = MAX_MESSAGES - 1;
        if (s_ui.current_msg_idx > 0) {
            s_ui.current_msg_idx--;
        }
    }
    
    ui_message_t* msg = &s_ui.messages[s_ui.message_count];
    strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    msg->sender[sizeof(msg->sender) - 1] = '\0';
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text) - 1] = '\0';
    msg->timestamp = board_time_ms();
    msg->is_read = false;
    
    s_ui.message_count++;
    // 新消息到来，自动定位到最新
    s_ui.current_msg_idx = s_ui.message_count - 1;
}

/* ================== 核心接口实现 ================== */

void ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    // 默认启动进入主界面，而不是待机模式，这样用户能看到屏幕亮起
    s_ui.state = UI_STATE_MAIN;
    ui_update_activity();

    ESP_LOGI(UI_TAG, "UI组件初始化完成");
}

void ui_tick(void)
{
    // 自动待机检查
    if (s_ui.state != UI_STATE_STANDBY) {
        if (board_time_ms() - s_ui.last_activity_time > STANDBY_TIMEOUT_MS) {
            ui_enter_standby();
            return;
        }
    }
    
    // 渲染逻辑
    switch (s_ui.state) {
        case UI_STATE_STANDBY:
            // 待机状态不刷新屏幕，保持黑屏
            break;
            
        case UI_STATE_MAIN: {
            int unread = 0;
            for(int i=0; i<s_ui.message_count; i++) {
                if(!s_ui.messages[i].is_read) unread++;
            }
            ui_render_main(s_ui.message_count, unread);
            break;
        }
            
        case UI_STATE_MESSAGE_READ: {
            if (s_ui.message_count > 0) {
                ui_message_t* msg = &s_ui.messages[s_ui.current_msg_idx];
                // 如果未读，标记为已读
                if (!msg->is_read) {
                    msg->is_read = true;
                    // 可以在这里发送回执等
                }
                ui_render_message_read(msg, s_ui.current_msg_idx, s_ui.message_count);
            }
            break;
        }
            
        default:
            break;
    }
}

void ui_on_key(board_key_t key)
{
    ui_update_activity();
    
    // 如果在待机，任何按键唤醒
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_wake_up();
        return;
    }
    
    switch (s_ui.state) {
        case UI_STATE_MAIN:
            if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN) {
                if (s_ui.message_count > 0) {
                    s_ui.state = UI_STATE_MESSAGE_READ;
                    // 默认显示最新一条
                    s_ui.current_msg_idx = s_ui.message_count - 1;
                }
            }
            break;
            
        case UI_STATE_MESSAGE_READ:
            if (key == BOARD_KEY_BACK) {
                s_ui.state = UI_STATE_MAIN;
            } else if (key == BOARD_KEY_DOWN) {
                // 下一条（更新的消息）
                if (s_ui.current_msg_idx < s_ui.message_count - 1) {
                    s_ui.current_msg_idx++;
                } else {
                    // 循环到第一条
                    s_ui.current_msg_idx = 0;
                }
            } else if (key == BOARD_KEY_UP) {
                // 上一条（更旧的消息）
                if (s_ui.current_msg_idx > 0) {
                    s_ui.current_msg_idx--;
                } else {
                    // 循环到最后一条
                    s_ui.current_msg_idx = s_ui.message_count - 1;
                }
            }
            break;
            
        default:
            break;
    }
}

void ui_show_message(const char* sender, const char* text)
{
    ui_add_message_internal(sender, text);
    
    // 唤醒并跳转到消息阅读
    ui_wake_up();
    s_ui.state = UI_STATE_MESSAGE_READ;
    s_ui.current_msg_idx = s_ui.message_count - 1;
    
    // 触发反馈
    board_notify();
}

void ui_enter_standby(void)
{
    if (s_ui.state != UI_STATE_STANDBY) {
        s_ui.state = UI_STATE_STANDBY;
        ui_render_standby();
        board_rgb_off();
        ESP_LOGI(UI_TAG, "进入待机模式");
    }
}

void ui_wake_up(void)
{
    if (s_ui.state == UI_STATE_STANDBY) {
        s_ui.state = UI_STATE_MAIN;
        ui_update_activity();
        board_vibrate_on(50);
        ESP_LOGI(UI_TAG, "唤醒屏幕");
    }
}
