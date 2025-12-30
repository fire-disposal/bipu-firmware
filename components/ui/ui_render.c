#include "ui_render.h"
#include "board.h"
#include "ble_manager.h"
#include <string.h>
#include <stdio.h>

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

static void ui_render_status_bar(const char* right_text)
{
    // 绘制顶部状态栏背景
    // board_display_rect(0, 0, 128, 12, true); // 如果需要反色背景
    
    // 绘制分割线
    board_display_rect(0, 12, 128, 1, true);
    
    // 显示BLE状态
    bool connected = ble_manager_is_connected();
    board_display_text(2, 10, connected ? "BLE: OK" : "BLE: --");
    
    // 右对齐显示文本 (假设字符宽度约6px)
    if (right_text) {
        board_display_text(90, 10, right_text);
    }
}

void ui_render_main(int message_count, int unread_count)
{
    board_display_begin();
    
    ui_render_status_bar(NULL);
    
    // 显示欢迎语或大时钟
    board_display_text(30, 35, "BIPI PAGER");
    
    // 显示消息计数
    char msg_info[64];
    if (message_count > 0) {
        snprintf(msg_info, sizeof(msg_info), "消息: %d (未读: %d)", message_count, unread_count);
    } else {
        snprintf(msg_info, sizeof(msg_info), "暂无消息");
    }
    board_display_text(10, 55, msg_info);
    
    board_display_end();
}

void ui_render_message_read(const ui_message_t* msg, int current_idx, int total_count)
{
    if (!msg) return;
    
    board_display_begin();
    
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "[%d/%d]", current_idx + 1, total_count);
    ui_render_status_bar(page_str);
    
    // 显示发送者
    char header[64];
    snprintf(header, sizeof(header), "From: %s", msg->sender);
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
    
    board_display_end();
}

void ui_render_standby(void)
{
    board_display_begin();
    board_display_end(); // 清屏
}
