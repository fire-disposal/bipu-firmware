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
    /* 长按变体：key.c 在按键持续超过 LONG_PRESS_MS 时生成，
     * 与短按事件在同一队列中以不同枚举值区分，
     * 使页面代码无需自行计时检测长按。 */
    BOARD_KEY_UP_LONG,
    BOARD_KEY_DOWN_LONG,
    BOARD_KEY_ENTER_LONG,
    BOARD_KEY_BACK_LONG,
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
/**
 * @brief 注册预刷新钩子（在 board_display_end → SendBuffer 之前调用）
 * 用于在任意页面渲染帧的顶层叠加 Toast / HUD，无需修改各页面 render 函数。
 * 钩子在 display 互斥锁持有期间被调用，只应使用 board_display_* 绘制接口。
 * @param cb 钩子函数指针，传 NULL 可注销
 */
void board_display_set_pre_flush_cb(void (*cb)(void));

// 震动接口 (统一为异步非阻塞风格)
void board_vibrate_short(void);
void board_vibrate_double(void);
void board_vibrate_pattern(const uint32_t* ms_array, uint8_t count);
void board_vibrate_off(void);
void board_vibrate_tick(void); // 必须在 main loop 调用
void board_vibrate_test_direct(uint32_t duration_ms); // 测试用：直接GPIO输出

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

/**
 * @brief 从 NVS 恢复上次 BLE 时间同步的系统时间
 * 
 * 在启动时调用此函数恢复系统时间。如果存在同步记录，则：
 *   恢复时间 = 上次同步时间 + (当前 esp_timer - 同步时的 esp_timer)
 * 
 * 如果无同步记录（首次启动），系统时间初始化为 Unix Epoch，
 * 直到首次收到 BLE 时间同步消息。
 */
void board_restore_time_from_sync(void);

/* ================== 反馈接口 ================== */
void board_notify(void);

/* ================== 电源管理接口 ================== */
/* 低电压保护功能已弃用。电压读取请使用 board_battery_voltage() / board_battery_percent()。 */


/* ================== 三个独立白光 LED 接口（双层状态机 v2）================== */
/*
 * 优先级（从高到低）：
 *   FLASHLIGHT → board_leds_flashlight_on/off()  三灯全亮覆盖
 *   FOREGROUND → 一次性效果（短闪/通知），完成后自动续接背景
 *   BACKGROUND → 持久背景（跑马/慢闪/常灭），由 board_leds_set_mode() 设置
 */
void board_leds_init(void);
void board_leds_tick(void);                    /* 主循环每 10ms 调用 */
void board_leds_set_mode(board_led_mode_t mode);
void board_leds_flashlight_on(void);
void board_leds_flashlight_off(void);
bool board_leds_is_flashlight_on(void);
void board_leds_off(void);
bool board_leds_is_active(void);
bool board_leds_is_initialized(void);
/* 遗留 API（保持签名兼容，内部转发至新状态机） */
void board_leds_set(board_leds_t leds);
void board_leds_short_flash(void);
void board_leds_double_flash(void);
void board_leds_continuous_blink_start(void);
void board_leds_continuous_blink_stop(void);
void board_leds_gallop_start(void);
void board_leds_gallop_stop(void);

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
