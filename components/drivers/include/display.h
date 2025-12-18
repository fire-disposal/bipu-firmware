#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ================== 基础类型 ================== */

typedef struct {
    uint8_t width;
    uint8_t height;
} display_size_t;

typedef enum {
    DISPLAY_FONT_SMALL = 0,
    DISPLAY_FONT_NORMAL,
    DISPLAY_FONT_LARGE,
} display_font_t;

/* ================== 生命周期 ================== */

/**
 * 初始化显示系统（同步）
 * - 初始化 I2C
 * - 初始化 u8g2
 * - 打开屏幕电源
 */
void display_init(void);

/**
 * 开始一帧绘制
 * 必须在一切 draw 之前调用
 */
void display_begin_frame(void);

/**
 * 提交当前帧到屏幕
 * 必须在一切 draw 之后调用
 */
void display_end_frame(void);

/* ================== 基础绘制能力 ================== */

display_size_t display_get_size(void);

void display_set_font(display_font_t font);

void display_draw_text(int x, int y, const char *text);

void display_draw_rect(int x, int y, int w, int h, bool fill);

void display_draw_hline(int x, int y, int w);

void display_draw_vline(int x, int y, int h);

void display_clear(void);
