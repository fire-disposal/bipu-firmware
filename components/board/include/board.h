#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "board_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 数据类型定义 ================== */

typedef enum {
    BOARD_KEY_NONE = -1,
    BOARD_KEY_UP,
    BOARD_KEY_DOWN,
    BOARD_KEY_ENTER,
    BOARD_KEY_BACK,
} board_key_t;

typedef struct {
    uint8_t led1; 
    uint8_t led2; 
    uint8_t led3; 
} board_leds_t;

/* ================== 核心生命周期 ================== */

esp_err_t board_init(void);    // 内部应自动调用所有私有 init 函数
uint32_t  board_time_ms(void);
void      board_delay_ms(uint32_t ms);

/* ================== 交互接口 (UI/Feedback) ================== */

// 显示接口
void board_display_begin(void);
void board_display_end(void);
void board_display_text(int x, int y, const char* text);
void board_display_rect(int x, int y, int w, int h, bool fill);
void board_display_glyph(int x, int y, uint16_t encoding);
void board_display_set_font(const void* font);
int board_display_text_width(const char* text);
void board_display_set_contrast(uint8_t contrast);  // OLED亮度/对比度控制
void board_display_set_draw_color(uint8_t color);   // 0=黑 1=白 2=XOR
void board_display_set_font_mode(uint8_t mode);     // 0=实心 1=透明

// 震动接口 (统一为异步非阻塞风格)
void board_vibrate_short(void);
void board_vibrate_double(void);
void board_vibrate_pattern(const uint32_t* ms_array, uint8_t count);
void board_vibrate_off(void);
void board_vibrate_tick(void); // 必须在 main loop 调用

// LED 接口
typedef enum {
    BOARD_LED_MODE_OFF,              // 关闭
    BOARD_LED_MODE_STATIC,           // 静态点亮（用于手电筒）
    BOARD_LED_MODE_MARQUEE,          // 跑马灯（轮流点亮）
    BOARD_LED_MODE_BLINK,            // 闪烁（亮灭交替）
    BOARD_LED_MODE_NOTIFY_FLASH,     // 通知闪烁（快速闪两次）
} board_led_mode_t;

void board_leds_set(board_leds_t leds);
void board_leds_off(void);
void board_leds_tick(void);  // 周期调用以更新 LED 状态（需在主循环中调用）
void board_leds_set_mode(board_led_mode_t mode);  // 设置 LED 工作模式
void board_leds_notify(void);  // LED 通知闪烁（快速闪两次，优先级最高）

/* ================== 输入与传感器 ================== */

board_key_t board_key_poll(void);
bool        board_key_is_pressed(board_key_t key);      // 检查按键是否正被按下
bool        board_key_is_long_pressed(board_key_t key); // 检查是否长按中
uint32_t    board_key_press_duration(board_key_t key); // 获取按下持续时间(ms)
float       board_battery_voltage(void);
uint8_t     board_battery_percent(void);
bool        board_battery_is_charging(void);

/* ================== RTC 与 系统 ================== */

esp_err_t board_set_rtc(uint16_t year, uint8_t month, uint8_t day, 
                        uint8_t hour, uint8_t minute, uint8_t second);
esp_err_t board_set_rtc_from_timestamp(time_t timestamp);
void      board_system_restart(void);

// 通用通知接口
void board_notify(void);

#ifdef __cplusplus
}
#endif
