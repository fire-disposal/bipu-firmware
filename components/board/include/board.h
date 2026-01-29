#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 按键类型定义 */
typedef enum {
    BOARD_KEY_NONE = -1,
    BOARD_KEY_UP,
    BOARD_KEY_DOWN,
    BOARD_KEY_ENTER,
    BOARD_KEY_BACK,
} board_key_t;

/* RGB颜色定义 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} board_rgb_t;

/* ================== 生命周期 ================== */
esp_err_t board_init(void);

/* ================== 显示接口 ================== */
void board_display_begin(void);
void board_display_end(void);
void board_display_text(int x, int y, const char* text);
void board_display_rect(int x, int y, int w, int h, bool fill);

/* ================== 输入接口 ================== */
board_key_t board_key_poll(void);

/* ================== 时间接口 ================== */
uint32_t board_time_ms(void);
void board_delay_ms(uint32_t ms);

/* ================== 反馈接口 ================== */
void board_notify(void);
    
/* ================== 震动接口 ================== */
void board_vibrate_init(void);
esp_err_t board_vibrate_on(uint32_t ms);
esp_err_t board_vibrate_off(void);
void board_vibrate_tick(void);  // 需要在主循环中调用

/* ================== RGB灯接口 ================== */
void board_rgb_init(void);
void board_rgb_set(board_rgb_t color);
void board_rgb_off(void);

/* ================== 电源管理接口 ================== */
float board_battery_voltage(void);
uint8_t board_battery_percent(void);

