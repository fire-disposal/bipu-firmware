#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "ui_types.h"
#include "ui_text.h"
#include "board.h"
#include "u8g2.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "PAGE_MSG";

// 滚动和显示参数
#define LINE_HEIGHT 12
#define MAX_SCROLL 200
#define SCROLL_STEP 12
#define HEADER_HEIGHT 28
#define CONTENT_START_Y 40

static int s_vertical_offset = 0;
static int s_content_height = 0;  // 内容总高度

static void page_on_enter(void) {
    ESP_LOGD(TAG, "Entering Message Page");
    s_vertical_offset = 0;
    s_content_height = 0;
}

static void page_on_exit(void) {
    // 清理状态
}

// 计算消息内容的总高度
static int calculate_content_height(const char* text, int area_width) {
    if (!text || !text[0]) return LINE_HEIGHT;
    
    int total_height = 0;
    const char *p = text;
    char line_buf[128];
    
    while (*p) {
        int pos = 0;
        int i = 0;
        while (p[i] != '\0') {
            unsigned char c = (unsigned char)p[i];
            int char_len = 1;
            if (c < 0x80) char_len = 1;
            else if ((c & 0xE0) == 0xC0) char_len = 2;
            else if ((c & 0xF0) == 0xE0) char_len = 3;
            else if ((c & 0xF8) == 0xF0) char_len = 4;

            if (pos + char_len >= (int)sizeof(line_buf) - 1) break;
            memcpy(&line_buf[pos], &p[i], char_len);
            pos += char_len;
            line_buf[pos] = '\0';

            int w = board_display_text_width(line_buf);
            if (w > area_width) {
                // 回退
                while (pos > 0 && ((unsigned char)line_buf[pos-1] & 0xC0) == 0x80) pos--;
                if (pos > 0) pos--;
                break;
            }
            i += char_len;
        }
        if (pos == 0 && p[0]) {
            i = 1;
        }
        total_height += LINE_HEIGHT;
        p += (i > 0 ? i : 1);
    }
    
    return total_height > 0 ? total_height : LINE_HEIGHT;
}

// 渲染上下文
typedef struct {
    ui_message_t msg;
    bool valid;
    int idx;
    int total;
    int vertical_offset;
    int content_height; // Pre-calculated in update
} msg_render_ctx_t;

static msg_render_ctx_t s_ctx;
static int s_cached_msg_idx = -1; // 用于避免重复计算高度

static uint32_t update(void) {
    int count = ui_get_message_count();
    if (count <= 0) {
        ui_change_page(UI_STATE_MAIN);
        return 1000;
    }

    int idx = ui_get_current_message_idx();
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    ui_set_current_message_idx(idx);

    ui_message_t* msg = ui_get_message_at(idx);
    
    // 填充上下文
    s_ctx.idx = idx;
    s_ctx.total = count;
    s_ctx.vertical_offset = s_vertical_offset;
    s_ctx.valid = (msg != NULL);

    if (msg) {
        // 标记已读
        if (!msg->is_read) {
            msg->is_read = true;
        }
        
        // 复制消息内容
        s_ctx.msg = *msg; // 结构体复制

        // 优化：仅在消息切换时计算高度
        if (idx != s_cached_msg_idx) {
            const int area_width = 128 - 2 - 4;
            s_content_height = calculate_content_height(msg->text, area_width);
            s_cached_msg_idx = idx;
        }
        s_ctx.content_height = s_content_height;
    }

    return 1000;
}

static void render(void) {
    if (!s_ctx.valid) return;
    
    // 使用 s_ctx 中的数据进行渲染，不再调用 ui_get_message_at
    // 也不再实时计算高度，直接使用 s_ctx.content_height
    
    board_display_begin();
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // ... (渲染代码调整为使用 s_ctx)
    // 顶部状态栏
    board_display_rect(0, 12, 128, 1, true);
    
    char idx_str[16];
    snprintf(idx_str, sizeof(idx_str), "%d/%d", s_ctx.idx + 1, s_ctx.total);
    board_display_text(4, 10, idx_str);
    
    time_t ts = (time_t)s_ctx.msg.timestamp;
    struct tm *tmv = localtime(&ts);
    if (tmv) {
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M", tmv);
        int tw = board_display_text_width(timestr);
        board_display_text(124 - tw, 10, timestr);
    }
    
    board_display_text(2, 24, "来自:");
    ui_draw_text_clipped(32, 24, 94, s_ctx.msg.sender[0] ? s_ctx.msg.sender : "未知");
    board_display_rect(0, 27, 128, 1, true);
    
    // 消息内容区域
    const int left = 2;
    // const int right = 4;
    // const int area_width = 128 - left - right;
    const int visible_height = 64 - CONTENT_START_Y;
    
    const char *p = s_ctx.msg.text;
    int y = CONTENT_START_Y - s_ctx.vertical_offset;
    char line_buf[128];
    
    // ... (保持原有的分行渲染逻辑，但数据源为 s_ctx.msg.text)
    while (*p) {
        // ... (原逻辑)
        int pos = 0;
        int i = 0;
        while (p[i] != '\0') {
             // ... (复制原逻辑)
            unsigned char c = (unsigned char)p[i];
            int char_len = 1;
            if (c < 0x80) char_len = 1;
            else if ((c & 0xE0) == 0xC0) char_len = 2;
            else if ((c & 0xF0) == 0xE0) char_len = 3;
            else if ((c & 0xF8) == 0xF0) char_len = 4;

            if (pos + char_len >= (int)sizeof(line_buf) - 1) break;
            memcpy(&line_buf[pos], &p[i], char_len);
            pos += char_len;
            line_buf[pos] = '\0';

            int w = board_display_text_width(line_buf);
            if (w > (128 - 2 - 4)) {
                while (pos > 0 && ((unsigned char)line_buf[pos-1] & 0xC0) == 0x80) pos--;
                if (pos > 0) pos--;
                line_buf[pos] = '\0';
                break;
            }
            i += char_len;
        }

        if (pos == 0 && p[0]) {
            unsigned char c = (unsigned char)p[0];
            int clen = 1;
            if ((c & 0xE0) == 0xC0) clen = 2;
            else if ((c & 0xF0) == 0xE0) clen = 3;
            else if ((c & 0xF8) == 0xF0) clen = 4;
            memcpy(line_buf, p, clen);
            line_buf[clen] = '\0';
            i = clen;
        }

        if (y >= CONTENT_START_Y - LINE_HEIGHT && y < 64) {
            board_display_text(left, y, line_buf);
        }

        y += LINE_HEIGHT;
        p += (i > 0 ? i : 1);
    }
    
    // 滚动指示器
    int max_scroll = s_ctx.content_height - visible_height;
    if (max_scroll < 0) max_scroll = 0;
    
    if (s_ctx.content_height > visible_height) {
        int scrollbar_height = 64 - CONTENT_START_Y;
        int thumb_height = (visible_height * scrollbar_height) / s_ctx.content_height;
        if (thumb_height < 4) thumb_height = 4;
        
        int thumb_y = CONTENT_START_Y;
        if (max_scroll > 0) {
            thumb_y = CONTENT_START_Y + ((s_ctx.vertical_offset * (scrollbar_height - thumb_height)) / max_scroll);
        }
        
        board_display_rect(126, CONTENT_START_Y, 2, scrollbar_height, false);
        board_display_rect(126, thumb_y, 2, thumb_height, true);
    }
    
    if (!s_ctx.msg.is_read) {
        board_display_text(114, 24, "新");
    }
    
    board_display_end();
}

static void on_key(board_key_t key) {
    int count = ui_get_message_count();
    int idx = ui_get_current_message_idx();
    
    ESP_LOGD(TAG, "Message page key: %d, idx: %d/%d, scroll: %d", key, idx, count, s_vertical_offset);

    // 计算最大滚动量
    const int visible_height = 64 - CONTENT_START_Y;
    int max_scroll = s_content_height - visible_height;
    if (max_scroll < 0) max_scroll = 0;

    switch (key) {
        case BOARD_KEY_BACK:
            ESP_LOGD(TAG, "Returning to list");
            ui_change_page(UI_STATE_MESSAGE_LIST);
            break;
            
        case BOARD_KEY_DOWN:
            // 向下滚动，如果已到底部则切换到下一条
            if (s_vertical_offset < max_scroll) {
                s_vertical_offset += SCROLL_STEP;
                if (s_vertical_offset > max_scroll) s_vertical_offset = max_scroll;
            } else if (idx < count - 1) {
                // 切换到下一条消息
                ui_set_current_message_idx(idx + 1);
                s_vertical_offset = 0;
            }
            break;
            
        case BOARD_KEY_UP:
            // 向上滚动，如果已到顶部则切换到上一条
            if (s_vertical_offset > 0) {
                s_vertical_offset -= SCROLL_STEP;
                if (s_vertical_offset < 0) s_vertical_offset = 0;
            } else if (idx > 0) {
                // 切换到上一条消息
                ui_set_current_message_idx(idx - 1);
                s_vertical_offset = 0;
            }
            break;
            
        case BOARD_KEY_ENTER:
            // 确认键：切换到下一条消息
            if (idx < count - 1) {
                ui_set_current_message_idx(idx + 1);
            } else {
                ui_set_current_message_idx(0);
            }
            s_vertical_offset = 0;
            break;
            
        default:
            break;
    }
}

const ui_page_t page_message = {
    .on_enter = page_on_enter,
    .on_exit = page_on_exit,
    .update = update,
    .render = render,
    .on_key = on_key
};
