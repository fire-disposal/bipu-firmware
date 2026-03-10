#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_icons.h
 * @brief 统一的UI图标管理模块
 * 
 * 包含：
 * 1. Open Iconic 图标字体的编码常量
 * 2. 高层便利函数（自动处理字体切换和坐标）
 * 
 * 所有UI状态指示器（电池、BLE、充电、手电筒等）都通过此模块集中管理
 */

/* ================== Open Iconic 图标编码定义 ================== */
/* 
 * 使用 u8g2_font_open_iconic_* 系列字体
 * 通过 board_display_glyph(x, y, encoding) 函数绘制
 * 需要先设置字体：board_display_set_font(font)
 */

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
#define ICON_CHECK_1X       115 // 勾选图标
#define ICON_CHECK_2X       116 // 勾选图标 (2x)
#define ICON_CHECK_4X       117 // 勾选图标 (4x)

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

/* ================== 高层便利函数（自动处理字体切换） ================== */

/**
 * @brief 绘制电池指示图标（22px 宽 x 10px 高）
 * 
 * 显示当前电量百分比和充电状态
 * 
 * @param x 左上角X坐标
 * @param y 左上角Y坐标
 */
void ui_icon_draw_battery(int x, int y);

/**
 * @brief 绘制BLE连接状态图标（14px 宽）
 * 
 * 当已连接时显示 "BT"（大写），广播时显示 "bt"（小写）
 * 
 * @param x 左上角X坐标
 * @param y 左上角Y坐标
 */
void ui_icon_draw_ble(int x, int y);

/**
 * @brief 绘制充电指示图标（8px 宽）
 * 
 * 仅当设备正在充电时显示闪电符号 "⚡"
 * 
 * @param x 左上角X坐标
 * @param y 左上角Y坐标
 */
void ui_icon_draw_charging(int x, int y);

/**
 * @brief 绘制手电筒状态指示图标（8px 宽）
 * 
 * 手电筒开启时显示 "✓"，关闭时不显示
 * 
 * @param x 左上角X坐标
 * @param y 左上角Y坐标
 */
void ui_icon_draw_flashlight(int x, int y);

/* ================== 图标使用示例 ================== */
/*
 * 方式1：使用高层便利函数（推荐）
 *   ui_icon_draw_battery(100, 0);
 *   ui_icon_draw_ble(2, 0);
 *   ui_icon_draw_charging(30, 0);
 *   ui_icon_draw_flashlight(45, 0);
 * 
 * 方式2：使用原始编码（需要手动管理字体）
 *   board_display_set_font(u8g2_font_open_iconic_email_1x_t);
 *   board_display_glyph(10, 20, ICON_EMAIL_1X);
 *   board_display_set_font(u8g2_font_wqy12_t_gb2312a);  // 记得恢复字体
 *   board_display_text(25, 20, " 新消息");
 */

#ifdef __cplusplus
}
#endif
