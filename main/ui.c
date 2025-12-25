#include "ui.h"
#include "board.h"
#include <string.h>
#include <stdio.h>

// ================== 静态状态 ==================
static ui_state_t g_ui_state = {
    .state = UI_STATE_STANDBY,
    .message_count = 0,
    .current_msg_idx = 0,
    .last_activity_time = 0
};

// ================== 内部辅助函数 ==================
static void ui_add_message(const char* sender, const char* text)
{
    if (g_ui_state.message_count >= 10) {
        // 移除最旧的消息
        for (int i = 0; i < 9; i++) {
            g_ui_state.messages[i] = g_ui_state.messages[i + 1];
        }
        g_ui_state.message_count = 9;
        if (g_ui_state.current_msg_idx > 0) {
            g_ui_state.current_msg_idx--;
        }
    }
    
    ui_message_t* msg = &g_ui_state.messages[g_ui_state.message_count];
    strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    msg->sender[sizeof(msg->sender) - 1] = '\0';
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text) - 1] = '\0';
    msg->timestamp = board_time_ms();
    msg->is_read = false;
    
    g_ui_state.message_count++;
    g_ui_state.current_msg_idx = g_ui_state.message_count - 1;
}

static void ui_render_standby(void)
{
    // 黑屏状态，不显示任何内容
    board_display_begin();
    board_display_end();
}

static void ui_render_main(void)
{
    board_display_begin();
    board_display_text(10, 20, "BIPI 已就绪");
    if (g_ui_state.message_count > 0) {
        char msg_info[32];
        snprintf(msg_info, sizeof(msg_info), "消息: %d条", g_ui_state.message_count);
        board_display_text(10, 35, msg_info);
    }
    board_display_end();
}

static void ui_render_message(void)
{
    if (g_ui_state.message_count == 0) {
        board_display_begin();
        board_display_text(10, 30, "暂无消息");
        board_display_end();
        return;
    }
    
    ui_message_t* msg = &g_ui_state.messages[g_ui_state.current_msg_idx];
    board_display_begin();
    
    // 显示消息头信息
    char header[32];
    snprintf(header, sizeof(header), "%d/%d %s", 
             g_ui_state.current_msg_idx + 1,
             g_ui_state.message_count,
             msg->sender);
    board_display_text(10, 15, header);
    
    // 显示消息内容（简单分行处理）
    char line1[32] = {0};
    char line2[32] = {0};
    char line3[32] = {0};
    
    int text_len = strlen(msg->text);
    if (text_len > 0) {
        // 第一行（21字符）
        int copy_len = (text_len > 21) ? 21 : text_len;
        strncpy(line1, msg->text, copy_len);
        line1[copy_len] = '\0';
        
        if (text_len > 21) {
            // 第二行（21字符）
            copy_len = (text_len > 42) ? 21 : (text_len - 21);
            strncpy(line2, msg->text + 21, copy_len);
            line2[copy_len] = '\0';
            
            if (text_len > 42) {
                // 第三行（剩余字符，最多21）
                copy_len = (text_len > 63) ? 21 : (text_len - 42);
                strncpy(line3, msg->text + 42, copy_len);
                line3[copy_len] = '\0';
            }
        }
    }
    
    board_display_text(10, 30, line1);
    if (line2[0]) board_display_text(10, 42, line2);
    if (line3[0]) board_display_text(10, 54, line3);
    
    // 显示阅读状态
    if (msg->is_read) {
        board_display_text(100, 15, "已读");
    }
    
    board_display_end();
}

// ================== 公共接口实现 ==================
ui_state_t* ui_get_state(void)
{
    return &g_ui_state;
}

UiState ui_get_current_state(void)
{
    return g_ui_state.state;
}

void ui_init(void)
{
    g_ui_state.state = UI_STATE_STANDBY;
    g_ui_state.message_count = 0;
    g_ui_state.current_msg_idx = 0;
    g_ui_state.last_activity_time = board_time_ms();
}

void ui_tick(void)
{
    // 自动返回待机状态（30秒无操作）
    if (g_ui_state.state != UI_STATE_STANDBY) {
        uint32_t current_time = board_time_ms();
        if (current_time - g_ui_state.last_activity_time > 30000) {
            ui_enter_standby();
            return;
        }
    }
    
    // 根据当前状态渲染界面
    switch (g_ui_state.state) {
        case UI_STATE_STANDBY:
            ui_render_standby();
            break;
        case UI_STATE_MAIN:
            ui_render_main();
            break;
        case UI_STATE_MESSAGE:
            ui_render_message();
            break;
    }
}

void ui_on_key(board_key_t key)
{
    // 更新活动时间
    g_ui_state.last_activity_time = board_time_ms();
    
    // 从待机状态唤醒
    if (g_ui_state.state == UI_STATE_STANDBY) {
        g_ui_state.state = UI_STATE_MAIN;
        board_vibrate_on(50); // 轻微震动反馈
        return;
    }
    
    // 处理按键
    switch (key) {
        case BOARD_KEY_UP:
            if (g_ui_state.state == UI_STATE_MESSAGE && g_ui_state.message_count > 0) {
                g_ui_state.current_msg_idx--;
                if (g_ui_state.current_msg_idx < 0) {
                    g_ui_state.current_msg_idx = g_ui_state.message_count - 1;
                }
                board_vibrate_on(30);
            }
            break;
            
        case BOARD_KEY_DOWN:
            if (g_ui_state.state == UI_STATE_MESSAGE && g_ui_state.message_count > 0) {
                g_ui_state.current_msg_idx++;
                if (g_ui_state.current_msg_idx >= g_ui_state.message_count) {
                    g_ui_state.current_msg_idx = 0;
                }
                board_vibrate_on(30);
            }
            break;
            
        case BOARD_KEY_ENTER:
            if (g_ui_state.state == UI_STATE_MAIN && g_ui_state.message_count > 0) {
                g_ui_state.state = UI_STATE_MESSAGE;
                board_vibrate_on(50);
            } else if (g_ui_state.state == UI_STATE_MESSAGE && g_ui_state.message_count > 0) {
                // 标记当前消息为已读
                g_ui_state.messages[g_ui_state.current_msg_idx].is_read = true;
                board_vibrate_on(30);
            }
            break;
            
        case BOARD_KEY_BACK:
            if (g_ui_state.state == UI_STATE_MESSAGE) {
                g_ui_state.state = UI_STATE_MAIN;
                board_vibrate_on(30);
            }
            break;
            
        default:
            break;
    }
}

void ui_show_message(const char* sender, const char* text)
{
    if (!sender || !text) return;
    
    // 添加新消息
    ui_add_message(sender, text);
    
    // 震动提醒
    board_vibrate_on(100);
    board_rgb_set_color(BOARD_RGB_BLUE);
    
    // 如果有新消息，自动切换到主界面
    if (g_ui_state.state == UI_STATE_STANDBY) {
        g_ui_state.state = UI_STATE_MAIN;
    }
}

void ui_enter_standby(void)
{
    g_ui_state.state = UI_STATE_STANDBY;
    board_rgb_off(); // 关闭RGB灯
}

void ui_wake_up(void)
{
    if (g_ui_state.state == UI_STATE_STANDBY) {
        g_ui_state.state = UI_STATE_MAIN;
        g_ui_state.last_activity_time = board_time_ms();
        board_vibrate_on(50);
    }
}

void ui_next_message(void)
{
    if (g_ui_state.message_count == 0) return;
    
    g_ui_state.current_msg_idx++;
    if (g_ui_state.current_msg_idx >= g_ui_state.message_count) {
        g_ui_state.current_msg_idx = 0;
    }
}

void ui_prev_message(void)
{
    if (g_ui_state.message_count == 0) return;
    
    g_ui_state.current_msg_idx--;
    if (g_ui_state.current_msg_idx < 0) {
        g_ui_state.current_msg_idx = g_ui_state.message_count - 1;
    }
}