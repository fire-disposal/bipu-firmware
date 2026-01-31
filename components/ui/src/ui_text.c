#include "ui_text.h"
#include "board.h"
#include "u8g2.h"
#include <string.h>

// 查找 idx 之前最近的 UTF-8 字符起始位置
static int prev_utf8_start(const char* s, int idx) {
    while (idx > 0) {
        idx--;
        if (((unsigned char)s[idx] & 0xC0) != 0x80) break;
    }
    return idx;
}

void ui_draw_text_clipped(int x, int y, int max_width, const char* text) {
    if (!text) return;
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    int text_w = board_display_text_width(text);
    if (text_w <= max_width) {
        board_display_text(x, y, text);
        return;
    }

    char buf[128];
    size_t src_len = strlen(text);
    int idx = (int)src_len;
    int ell_w = board_display_text_width("...");

    while (idx > 0) {
        idx = prev_utf8_start(text, idx);
        size_t copy_len = (size_t)idx;
        if (copy_len >= sizeof(buf) - 4) copy_len = sizeof(buf) - 4;
        memcpy(buf, text, copy_len);
        buf[copy_len] = '\0';
        int w = board_display_text_width(buf);
        if (w + ell_w <= max_width) break;
        if (idx == 0) break;
    }

    // 确保 buf 不以不完整的 UTF-8 字节结尾
    while (strlen(buf) > 0 && (((unsigned char)buf[strlen(buf)-1] & 0xC0) == 0x80)) {
        ((char*)buf)[strlen(buf)-1] = '\0';
    }
    strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
    board_display_text(x, y, buf);
}

void ui_draw_text_centered(int area_x, int area_y, int area_width, const char* text) {
    if (!text) return;
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    int w = board_display_text_width(text);
    int tx = area_x + (area_width - w) / 2;
    if (tx < area_x) tx = area_x;
    if (w <= area_width) {
        board_display_text(tx, area_y, text);
    } else {
        ui_draw_text_clipped(area_x, area_y, area_width, text);
    }
}
