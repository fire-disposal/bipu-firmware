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

/* ================== LED 模式定义 ================== */
typedef enum {
    BOARD_LED_MODE_OFF,              // 关闭
    BOARD_LED_MODE_STATIC,           // 静态点亮（用于手电筒）
    BOARD_LED_MODE_MARQUEE,          // 跑马灯（轮流点亮）
    BOARD_LED_MODE_BLINK,            // 闪烁（亮灭交替）
    BOARD_LED_MODE_NOTIFY_FLASH,     // 通知闪烁（快速闪两次）
    BOARD_LED_MODE_ADVERTISING,      // 广播跑马灯
    BOARD_LED_MODE_CONNECTED,        // 连接闪烁
} board_led_mode_t;

/* ================== 核心生命周期 ================== */

uint32_t  board_time_ms(void);
void      board_delay_ms(uint32_t ms);

/* ================== 板级初始化函数 ================== */

esp_err_t board_init(void);
esp_err_t board_i2c_init(void);
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

/* ================== 电源管理接口 ================== */
/**
 * @brief 设置低电压保护回调
 * @param callback 回调函数，当电池电压低于阈值时调用
 * @note 回调函数中可以进行亮度调节等低功耗操作
 */
void board_power_set_low_voltage_callback(void (*callback)(float voltage, bool is_charging));

/**
 * @brief 启用/禁用自动低电压保护
 * @param enable true=启用，false=禁用
 * @note 启用后会自动监控电池电压并调用相应的回调函数
 */
void board_power_enable_auto_protection(bool enable);

/**
 * @brief 手动检查电池电压并触发保护（如果启用）
 * @note 可以用于手动触发电压检查，通常由定时器自动调用
 */
void board_power_check_voltage(void);

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

/**
 * @brief 设置 LED 模式
 * @param mode LED 模式
 * @note 此函数会覆盖当前 LED 状态机的任何效果
 */
void board_leds_set_mode(board_led_mode_t mode);

/**
 * @brief 启用/禁用 BLE 状态自动 LED 指示
 * @param enable true=启用，false=禁用
 * @note 启用后，硬件层会自动根据 BLE 状态设置 LED 模式
 */
void board_leds_enable_ble_auto_indicator(bool enable);

/**
 * @brief 设置 BLE 状态变化回调
 * @param connected_callback 连接状态变化回调
 * @param advertising_callback 广播状态变化回调
 * @note 当 BLE 状态变化时，硬件层会调用相应的回调函数
 */
void board_leds_set_ble_state_callbacks(
    void (*connected_callback)(bool connected),
    void (*advertising_callback)(bool advertising)
);

/* ================== 输入与传感器 ================== */

board_key_t board_key_poll(void);
bool        board_key_is_pressed(board_key_t key);      // 检查按键是否正被按下
bool        board_key_is_long_pressed(board_key_t key); // 检查是否长按中
uint32_t    board_key_press_duration(board_key_t key); // 获取按下持续时间(ms)
float       board_battery_voltage(void);
uint8_t     board_battery_percent(void);
bool        board_battery_is_charging(void);

/* ================== 电池管理接口 ================== */
// 已移除：电池管理逻辑已上移至 app 层


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
