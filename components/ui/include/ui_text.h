#pragma once

#include <stdint.h>

// 在指定最大像素宽度内绘制文本，超出的部分以省略号替换（UTF-8 安全）
void ui_draw_text_clipped(int x, int y, int max_width, const char* text);

// 在指定区域内水平居中绘制文本（会自动使用 max_width = area_width）
void ui_draw_text_centered(int area_x, int area_y, int area_width, const char* text);
