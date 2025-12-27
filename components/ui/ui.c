#include "ui.h"
#include "board.h"
#include "ble_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* UI_TAG = "ui";

#define MAX_MESSAGES 10
#define STANDBY_TIMEOUT_MS 30000

/* ================== 内部状态定义 ================== */

typedef struct {
    char sender[32];
    char text[128];
    uint32_t timestamp;
    bool is_read;
} ui_message_t;

typedef struct {
    ui_state_enum_t state;
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
} ui_context_t;

static ui_context_t s_ui;

/* ================== 辅助函数 ================== */

static size_t ui_get_utf8_safe_len(const char* text, size_t max_bytes)
{
    size_t i = 0;
    while (text[i] != '\0' && i < max_bytes) {
        size_t char_len = 0;
        unsigned char c = (unsigned char)text[i];
        
        if (c < 0x80) char_len = 1;
        else if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        else char_len = 1; // 无效字符，按1字节处理
        
        if (i + char_len > max_bytes) {
            break; // 放不下了
        }
        
        i += char_len;
    }
    return i;
}

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

/* ================== 渲染函数 ================== */

static void ui_render_status_bar(void)
{
    // 绘制顶部状态栏背景
    // board_display_rect(0, 0, 128, 12, true); // 如果需要反色背景
    
    // 绘制分割线
    board_display_rect(0, 12, 128, 1, true);
    
    // 显示BLE状态
    bool connected = ble_manager_is_connected();
    board_display_text(2, 10, connected ? "BLE: OK" : "BLE: --");
    
    // 显示时间 (模拟时间，因为没有RTC，使用运行时间)
    uint32_t seconds = board_time_ms() / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu", (hours % 24), (minutes % 60));
    
    // 右对齐显示时间 (假设字符宽度约6px)
    board_display_text(90, 10, time_str);
}

static void ui_render_main(void)
{
    board_display_begin();
    
    ui_render_status_bar();
    
    // 显示欢迎语或大时钟
    board_display_text(30, 35, "BIPI PAGER");
    
    // 显示消息计数
    char msg_info[64];
    if (s_ui.message_count > 0) {
        int unread = 0;
        for(int i=0; i<s_ui.message_count; i++) {
            if(!s_ui.messages[i].is_read) unread++;
        }
        snprintf(msg_info, sizeof(msg_info), "消息: %d (未读: %d)", s_ui.message_count, unread);
    } else {
        snprintf(msg_info, sizeof(msg_info), "暂无消息");
    }
    board_display_text(10, 55, msg_info);
    
    board_display_end();
}

static void ui_render_message_read(void)
{
    if (s_ui.message_count == 0) return;
    
    ui_message_t* msg = &s_ui.messages[s_ui.current_msg_idx];
    
    board_display_begin();
    
    ui_render_status_bar();
    
    // 显示消息索引和发送者
    char header[64];
    snprintf(header, sizeof(header), "[%d/%d] %s", 
             s_ui.current_msg_idx + 1, s_ui.message_count, msg->sender);
    board_display_text(0, 25, header);
    
    // 显示消息内容（简单的多行显示）
    // 这里假设每行约20个字符（中文10个）
    char line_buf[32];
    const char* p = msg->text;
    int y = 38;
    
    while (*p && y < 64) {
        // 计算不截断UTF-8字符的安全长度
        size_t safe_len = ui_get_utf8_safe_len(p, 20);
        
        strncpy(line_buf, p, safe_len);
        line_buf[safe_len] = '\0';
        
        board_display_text(2, y, line_buf);
        y += 12;
        
        p += safe_len;
    }
    
    // 如果未读，标记为已读
    if (!msg->is_read) {
        msg->is_read = true;
        // 可以在这里发送回执等
    }
    
    board_display_end();
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
            
        case UI_STATE_MAIN:
            ui_render_main();
            break;
            
        case UI_STATE_MESSAGE_READ:
            ui_render_message_read();
            break;
            
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
        board_display_begin();
        board_display_end(); // 清屏
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
