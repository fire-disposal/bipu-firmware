#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

/* 按键类型定义 */
typedef enum {
    BOARD_KEY_NONE = -1,
    BOARD_KEY_UP,
    BOARD_KEY_DOWN,
    BOARD_KEY_ENTER,
    BOARD_KEY_BACK,
} board_key_t;

/* 三个独立白光 LED 定义 (替代原 RGB 语义) */
typedef struct {
    uint8_t led1; // 原 R 通道
    uint8_t led2; // 原 G 通道 (注意: 可能为 strapping pin)
    uint8_t led3; // 原 B 通道
} board_leds_t;

/* ================== 生命周期 ================== */
esp_err_t board_init(void);

/* ================== 显示接口 ================== */
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

/* ================== 输入接口 ================== */
board_key_t board_key_poll(void);
bool board_key_is_pressed(board_key_t key);      // 检查按键是否正被按下
bool board_key_is_long_pressed(board_key_t key); // 检查是否长按中
uint32_t board_key_press_duration(board_key_t key); // 获取按下持续时间(ms)

/* ================== 时间接口 ================== */
uint32_t board_time_ms(void);
void board_delay_ms(uint32_t ms);

/* ================== RTC (实时时钟) 接口 ================== */
/**
 * @brief 设置系统时间 (RTC)
 * @param year 年份 (2000-2099)
 * @param month 月份 (1-12)
 * @param day 日期 (1-31)
 * @param hour 小时 (0-23)
 * @param minute 分钟 (0-59)
 * @param second 秒钟 (0-59)
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t board_set_rtc(uint16_t year, uint8_t month, uint8_t day, 
                        uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief 从 time_t 时间戳设置 RTC
 * @param timestamp 时间戳 (从1970年1月1日起的秒数)
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t board_set_rtc_from_timestamp(time_t timestamp);

/* ================== 反馈接口 ================== */
void board_notify(void);

/* ================== 震动接口 (状态机) ================== */
void board_vibrate_init(void);
void board_vibrate_short(void);           // 短震动
void board_vibrate_double(void);          // 震动两次
void board_vibrate_off(void);
void board_vibrate_tick(void);            // 状态机轮询，需在主循环中调用
bool board_vibrate_is_active(void);

/* ================== 三个独立白光 LED 接口 (状态机) ================== */
void board_leds_init(void);
void board_leds_set(board_leds_t leds);
void board_leds_off(void);
board_leds_t board_leds_get_state(void);
bool board_leds_is_initialized(void);
// LED 状态机控制
void board_leds_flashlight_on(void);
void board_leds_flashlight_off(void);
bool board_leds_is_flashlight_on(void);
void board_leds_short_flash(void);        // 短闪一次
void board_leds_double_flash(void);       // 快速闪动 2 次
void board_leds_continuous_blink_start(void);
void board_leds_continuous_blink_stop(void);
void board_leds_gallop_start(void);
void board_leds_gallop_stop(void);
void board_leds_tick(void);               // 状态机轮询，需在主循环中调用
bool board_leds_is_active(void);          // 是否有活跃效果

/* ================== 电源管理接口 ================== */
float board_battery_voltage(void);
uint8_t board_battery_percent(void);
bool board_battery_is_charging(void);

