#pragma once

/* ================== UI 图标编码定义 ================== */
/* 
 * 使用 u8g2_font_open_iconic_* 系列字体
 * 通过 board_display_glyph(x, y, encoding) 函数绘制
 * 需要先设置字体：board_display_set_font(font)
 */

/* Open Iconic 图标字体系列 */
// #include "u8g2.h"
// extern const uint8_t u8g2_font_open_iconic_all_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_app_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_arrow_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_check_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_email_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_embedded_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_gui_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_human_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_mime_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_other_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_play_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_text_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_thing_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_weather_1x_t[];
// extern const uint8_t u8g2_font_open_iconic_www_1x_t[];

/* ================== 常用图标编码 ================== */

/* 嵌入式/硬件图标 (embedded) */
#define ICON_CHIP_1X        64  // 芯片图标
#define ICON_CHIP_2X        65  // 芯片图标 (2x)
#define ICON_CHIP_4X        66  // 芯片图标 (4x)
#define ICON_BLUETOOTH_1X   67  // 蓝牙图标
#define ICON_BLUETOOTH_2X   68  // 蓝牙图标 (2x)
#define ICON_BLUETOOTH_4X   69  // 蓝牙图标 (4x)

/* 天气图标 (weather) */
#define ICON_LIGHTNING_1X   70  // 闪电图标 (充电用)
#define ICON_LIGHTNING_2X   71  // 闪电图标 (2x)
#define ICON_LIGHTNING_4X   72  // 闪电图标 (4x)

/* 其他图标 (other) */
#define ICON_INFO_1X        72  // 信息图标
#define ICON_INFO_2X        73  // 信息图标 (2x)
#define ICON_INFO_4X        74  // 信息图标 (4x)

/* 检查图标 (check) */
#define ICON_CHECK_1X       72  // 勾选图标
#define ICON_CHECK_2X       73  // 勾选图标 (2x)
#define ICON_CHECK_4X       74  // 勾选图标 (4x)

/* 邮件图标 (email) */
#define ICON_EMAIL_1X       64  // 邮件图标
#define ICON_EMAIL_2X       65  // 邮件图标 (2x)
#define ICON_EMAIL_4X       66  // 邮件图标 (4x)

/* 用户图标 (human) */
#define ICON_USER_1X        72  // 用户图标
#define ICON_USER_2X        73  // 用户图标 (2x)
#define ICON_USER_4X        74  // 用户图标 (4x)

/* 箭头图标 (arrow) */
#define ICON_ARROW_UP_1X    64  // 向上箭头
#define ICON_ARROW_DOWN_1X  65  // 向下箭头
#define ICON_ARROW_LEFT_1X  66  // 向左箭头
#define ICON_ARROW_RIGHT_1X 67  // 向右箭头

/* ================== 图标使用示例 ================== */
/*
 * 使用步骤：
 * 1. 设置图标字体：board_display_set_font(u8g2_font_open_iconic_email_1x_t);
 * 2. 绘制图标：board_display_glyph(x, y, ICON_EMAIL_1X);
 * 3. 恢复文本字体：board_display_set_font(u8g2_font_wqy12_t_gb2312a);
 * 
 * 示例：
 *   board_display_set_font(u8g2_font_open_iconic_email_1x_t);
 *   board_display_glyph(10, 20, ICON_EMAIL_1X);
 *   board_display_set_font(u8g2_font_wqy12_t_gb2312a);
 *   board_display_text(25, 20, " 新消息");
 */