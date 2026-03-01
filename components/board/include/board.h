#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

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

uint32_t  board_time_ms(void);
void      board_delay_ms(uint32_t ms);

/* ================== 板级初始化函数 ================== */

esp_err_t board_i2c_init(void);
// Transmit helper: sends data in chunks with retry, returns esp_err_t
esp_err_t board_i2c_transmit_chunked(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, size_t chunk_size, TickType_t timeout);
void board_display_init(void);
void board_key_init(void);
void board_vibrate_init(void);
void board_leds_init(void);
void board_power_init(void);
extern i2c_master_bus_handle_t board_i2c_bus_handle;

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

/* ================== 输入与传感器 ================== */

board_key_t board_key_poll(void);
bool        board_key_is_pressed(board_key_t key);      // 检查按键是否正被按下
bool        board_key_is_long_pressed(board_key_t key); // 检查是否长按中
uint32_t    board_key_press_duration(board_key_t key); // 获取按下持续时间(ms)
float       board_battery_voltage(void);
uint8_t     board_battery_percent(void);
bool        board_battery_is_charging(void);

/* ================== 电池管理接口 ================== */

/**
 * @brief 电池管理初始化
 */
void board_battery_manager_init(void);

/**
 * @brief 电池状态轮询处理
 *
 * 该函数应定期调用，处理电池状态监测、低电压保护等功能
 */
void board_battery_manager_tick(void);

/**
 * @brief 获取当前电池电量百分比
 * @return 电池电量百分比 (0-100)
 */
uint8_t board_battery_manager_get_percent(void);

/**
 * @brief 获取当前电池电压
 * @return 电池电压 (单位: V)
 */
float board_battery_manager_get_voltage(void);

/**
 * @brief 检查电池是否在充电
 * @return true 表示正在充电，false 表示未充电
 */
bool board_battery_manager_is_charging(void);

/**
 * @brief 检查是否处于低电压模式
 * @return true 表示处于低电压模式
 */
bool board_battery_manager_is_low_voltage_mode(void);

/**
 * @brief 获取电池管理的更新间隔（用于节能管理）
 * @param is_usb_power 当前供电方式
 * @return 推荐的检测间隔（毫秒）
 */
uint32_t board_battery_manager_get_update_interval(bool is_usb_power);

/* ================== RTC 与 系统 ================== */

esp_err_t board_set_rtc(uint16_t year, uint8_t month, uint8_t day, 
                        uint8_t hour, uint8_t minute, uint8_t second);
esp_err_t board_set_rtc_from_timestamp(time_t timestamp);
void      board_system_restart(void);

// 系统清理回调类型
typedef void (*board_cleanup_callback_t)(void);

/**
 * @brief 注册系统重启前的清理回调函数
 * @param callback 清理函数指针，在系统重启前调用
 */
void board_register_cleanup_callback(board_cleanup_callback_t callback);

/**
 * @brief 执行注册的清理回调函数
 * 通常在系统重启前调用，用于清理应用层资源
 */
void board_execute_cleanup(void);

/* 节能管理接口已移除：不再在运行时自动降低 I2C 频率或显示亮度。 */

// 通用通知接口
void board_notify(void);

#ifdef __cplusplus
}
#endif
