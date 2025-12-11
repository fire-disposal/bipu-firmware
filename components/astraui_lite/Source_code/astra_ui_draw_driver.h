//
// Created by Fir on 24-11-29.
//

#ifndef FUCKCLION_CORE_SRC_ASTRA_UI_LITE_DRAW_DRIVER_H_
#define FUCKCLION_CORE_SRC_ASTRA_UI_LITE_DRAW_DRIVER_H_

/* 此处自行添加头文件 */
#include "u8g2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* 此处自行添加头文件 */

/* 此处修改oled绘制函数 */
// 外部声明u8g2实例，将在main.c中定义
extern u8g2_t u8g2;

#define OLED_HEIGHT 64
#define OLED_WIDTH 128

// 时间相关函数
#define get_ticks() xTaskGetTickCount()
#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))

// 字体设置函数
#define oled_set_font(font) u8g2_SetFont(&u8g2, font)
#define oled_set_font_mode(mode) u8g2_SetFontMode(&u8g2, mode)
#define oled_set_font_direction(dir) u8g2_SetFontDirection(&u8g2, dir)

// 字符串绘制函数
#define oled_draw_str(x, y, str) u8g2_DrawStr(&u8g2, x, y, str)
#define oled_draw_UTF8(x, y, str) u8g2_DrawUTF8(&u8g2, x, y, str)
#define oled_get_str_width(str) u8g2_GetStrWidth(&u8g2, str)
#define oled_get_UTF8_width(str) u8g2_GetUTF8Width(&u8g2, str)
#define oled_get_str_height() u8g2_GetMaxCharHeight(&u8g2)

// 基本绘图函数
#define oled_draw_pixel(x, y) u8g2_DrawPixel(&u8g2, x, y)
#define oled_draw_circle(x, y, r, option) u8g2_DrawCircle(&u8g2, x, y, r, option)
#define oled_draw_R_box(x, y, w, h, r) u8g2_DrawRBox(&u8g2, x, y, w, h, r)
#define oled_draw_box(x, y, w, h) u8g2_DrawBox(&u8g2, x, y, w, h)
#define oled_draw_frame(x, y, w, h) u8g2_DrawFrame(&u8g2, x, y, w, h)
#define oled_draw_R_frame(x, y, w, h, r) u8g2_DrawRFrame(&u8g2, x, y, w, h, r)

// 线条绘制函数
#define oled_draw_H_line(x, y, l) u8g2_DrawHLine(&u8g2, x, y, l)
#define oled_draw_V_line(x, y, h) u8g2_DrawVLine(&u8g2, x, y, h)
#define oled_draw_line(x1, y1, x2, y2) u8g2_DrawLine(&u8g2, x1, y1, x2, y2)
#define oled_draw_H_dotted_line(x, y, l) u8g2_DrawHLine(&u8g2, x, y, l)  // u8g2没有虚线函数，用实线代替
#define oled_draw_V_dotted_line(x, y, h) u8g2_DrawVLine(&u8g2, x, y, h)  // u8g2没有虚线函数，用实线代替

// 位图绘制函数
#define oled_draw_bMP(x, y, w, h, bitMap) u8g2_DrawXBMP(&u8g2, x, y, w, h, bitMap)

// 颜色和缓冲区操作
#define oled_set_draw_color(color) u8g2_SetDrawColor(&u8g2, color)
#define oled_clear_buffer() u8g2_ClearBuffer(&u8g2)
#define oled_send_buffer() u8g2_SendBuffer(&u8g2)
#define oled_send_area_buffer(x, y, w, h) u8g2_SendBuffer(&u8g2)  // u8g2没有区域刷新，用全屏刷新代替
/* 此处修改oled绘制函数 */

extern void astra_ui_driver_init();

#endif //FUCKCLION_CORE_SRC_ASTRA_UI_LITE_DRAW_DRIVER_H_
