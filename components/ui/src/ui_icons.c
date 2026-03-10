#include "ui_icons.h"
#include "ble_manager.h"
#include "board.h"
#include "u8g2.h"
#include "ui.h"
#include <stdio.h>

/**
 * @brief 内部函数：绘制电池框和电量指示
 * 
 * 绘制电池的框体和内部电量填充百分比
 * 不包含充电符号（由 ui_icon_draw_charging 单独负责）
 */
static void draw_battery_bar(int x, int y)
{
    uint8_t pct = board_battery_percent();
    if (pct > 100) pct = 100;

    // 电池外框 (18px宽 x 9px高)
    board_display_rect(x, y + 2, 18, 9, false);
    
    // 电池突起 (正极)
    board_display_rect(x + 18, y + 4, 2, 5, true);

    // 电池电量填充 (使用14px的宽度来显示百分比)
    int fill_w = (14 * pct) / 100;
    if (fill_w > 0) {
        board_display_rect(x + 2, y + 4, fill_w, 5, true);
    }
}

/* ================== 公开的图标绘制接口 ================== */

void ui_icon_draw_battery(int x, int y)
{
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    draw_battery_bar(x, y);
}

void ui_icon_draw_ble(int x, int y)
{
    bool connected = ble_manager_is_connected();
    board_display_set_font(u8g2_font_6x13_tf);
    if (connected) {
        board_display_text(x, y, "BT");  // 已连接：大写
    } else {
        board_display_text(x, y, "bt...");  // 广播中：小写
    }
}

void ui_icon_draw_charging(int x, int y)
{
    if (board_battery_is_charging()) {
        board_display_set_font(u8g2_font_wqy12_t_gb2312a);
        board_display_text(x, y, "⚡");
    }
}

void ui_icon_draw_flashlight(int x, int y)
{
    if (ui_is_flashlight_on()) {
        board_display_set_font(u8g2_font_5x8_tr);
        board_display_text(x, y, "✓");
    }
}
