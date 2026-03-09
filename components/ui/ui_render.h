#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_render.h
 * @brief 统一渲染接口（组件化）
 * 
 * 设计原则：
 * 1. 封装底层 board_display_* 接口
 * 2. 提供可复用的高级组件
 * 3. 统一代码风格
 */

/* ================== 基础绘制原语 ================== */

/**
 * @brief 绘制文字
 * @param x X 坐标
 * @param y Y 坐标（基线）
 * @param text 文本（UTF-8 编码）
 */
void ui_draw_text(int x, int y, const char* text);

/**
 * @brief 绘制居中的文字
 * @param x 区域左上角 X
 * @param y 区域 Y 坐标（基线）
 * @param width 区域宽度
 * @param text 文本
 */
void ui_draw_text_centered(int x, int y, int width, const char* text);

/**
 * @brief 绘制矩形
 * @param x X 坐标
 * @param y Y 坐标
 * @param w 宽度
 * @param h 高度
 * @param fill true=填充，false=边框
 */
void ui_draw_rect(int x, int y, int w, int h, bool fill);

/**
 * @brief 绘制字符（图标）
 * @param x X 坐标
 * @param y Y 坐标
 * @param encoding 字符编码（u8g2 字体）
 */
void ui_draw_glyph(int x, int y, uint16_t encoding);

/**
 * @brief 绘制直线
 * @param x1 起点 X
 * @param y1 起点 Y
 * @param x2 终点 X
 * @param y2 终点 Y
 */
void ui_draw_line(int x1, int y1, int x2, int y2);

/**
 * @brief 设置字体
 * @param font 字体指针（u8g2 字体常量）
 */
void ui_set_font(const void* font);

/**
 * @brief 设置绘制颜色
 * @param color 0=黑色，1=白色，2=反色
 */
void ui_set_draw_color(uint8_t color);

/**
 * @brief 设置字体模式
 * @param mode 0=实心，1=透明
 */
void ui_set_font_mode(uint8_t mode);

/* ================== 高级组件 ================== */

/**
 * @brief 渲染状态栏（顶部）
 * @param center_text 中间文字（可选，NULL 表示不显示）
 */
void ui_render_status_bar(const char* center_text);

/**
 * @brief 列表配置
 */
typedef struct {
    const char** items;         /**< 列表项数组（NULL 结尾） */
    int item_count;             /**< 列表项数量 */
    int selected_index;         /**< 当前选中索引 */
    int scroll_offset;          /**< 滚动偏移（0=自动） */
    int items_per_page;         /**< 每页显示数量（0=自动） */
    const char* title;          /**< 标题（可选） */
} ui_list_config_t;

/**
 * @brief 渲染列表
 * @param config 列表配置
 */
void ui_render_list(ui_list_config_t* config);

/**
 * @brief 对话框按钮配置
 */
typedef struct {
    const char* text;           /**< 按钮文字 */
    bool is_default;            /**< 是否默认按钮 */
    bool is_cancel;             /**< 是否取消按钮 */
} ui_dialog_button_t;

/**
 * @brief 渲染对话框（居中弹窗）
 * @param title 标题
 * @param message 消息内容
 * @param buttons 按钮数组（NULL 结尾）
 */
void ui_render_dialog(const char* title, const char* message, 
                      ui_dialog_button_t buttons[]);

/**
 * @brief 简单对话框（快速版本）
 * @param title 标题
 * @param message 消息内容
 * @param button1 按钮 1 文字（可选）
 * @param button2 按钮 2 文字（可选）
 */
void ui_render_simple_dialog(const char* title, const char* message,
                             const char* button1, const char* button2);

/**
 * @brief 渲染 Toast 提示
 * @param message 提示文字
 */
void ui_render_toast(const char* message);

/**
 * @brief 渲染进度条
 * @param x X 坐标
 * @param y Y 坐标
 * @param width 宽度
 * @param height 高度
 * @param percent 百分比（0-100）
 */
void ui_render_progress_bar(int x, int y, int width, int height, uint8_t percent);

/**
 * @brief 渲染开关控件
 * @param x X 坐标
 * @param y Y 坐标
 * @param label 标签文字
 * @param is_on 开关状态
 */
void ui_render_toggle(int x, int y, const char* label, bool is_on);

/* ================== 布局辅助 ================== */

/**
 * @brief 计算文字宽度
 * @param text 文字
 * @return int 宽度（像素）
 */
int ui_text_width(const char* text);

/**
 * @brief 计算文字高度（当前字体）
 * @return int 高度（像素）
 */
int ui_text_height(void);

/**
 * @brief 计算居中 X 坐标
 * @param width 区域宽度
 * @param text 文字
 * @return int X 坐标
 */
int ui_center_x(int width, const char* text);

#ifdef __cplusplus
}
#endif
