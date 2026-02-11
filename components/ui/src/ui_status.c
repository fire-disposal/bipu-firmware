#include "ui_status.h"
#include "ble_manager.h"
#include "board.h"
#include "u8g2.h"
#include "ui.h"
#include "esp_log.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void draw_battery(int x, int y)
{
    uint8_t pct = board_battery_percent();
    if (pct > 100) pct = 100;

    // 电池外框
    board_display_rect(x, y + 2, 18, 9, false);
    board_display_rect(x + 18, y + 4, 2, 5, true);

    // 电池电量填充
    int fill_w = (14 * pct) / 100;
    if (fill_w > 0) {
        board_display_rect(x + 2, y + 4, fill_w, 5, true);
    }

    // 充电指示 (闪电符号)
    if (board_battery_is_charging()) {
        board_display_set_font(u8g2_font_wqy12_t_gb2312a);
        board_display_text(x - 10, y + 10, "⚡");
    }
}

void ui_status_render(const char* center_text)
{
    // 分割线
    board_display_rect(0, 12, 128, 1, true);

    // 左侧 BLE 状态指示（小字体）
    bool connected = ble_manager_is_connected();
    board_display_set_font(u8g2_font_5x8_tr);
    if (connected) {
        board_display_text(2, 9, "BT");
    } else {
        board_display_text(2, 9, "bt");
    }

    // 手电筒状态
    if (ui_is_flashlight_on()) {
        board_display_text(15, 9, "*");
    }

    // 中部文本或时间（小字体）
    char buf[32];
    const char* text = center_text;
    if (!text) {
        time_t now; time(&now);
        struct tm *t = localtime(&now);
        if (t) {
            strftime(buf, sizeof(buf), "%H:%M", t);
            text = buf;
        }
    }

    if (text) {
        const int left_margin = 22;
        const int right_margin = 24;
        const int area_x = left_margin;
        const int area_w = 128 - left_margin - right_margin;
        board_display_set_font(u8g2_font_6x13_tr);
        ui_draw_text_centered(area_x, 10, area_w, text);
    }

    // 右侧电池
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    draw_battery(106, 0);
}
