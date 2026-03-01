#pragma once
#include "driver/i2c_master.h"
#include "sdkconfig.h"

/* ================== 配置常量 ================== */
#define BOARD_TAG "board"

// 应用任务运行核心：单核芯片(C3)只能跑 Core 0
#define BOARD_APP_CPU   0

//屏幕配置
#define BOARD_I2C_FREQ_HZ         400000
#define BOARD_OLED_I2C_ADDRESS    0x3C
extern i2c_master_bus_handle_t board_i2c_bus_handle;

/* ESP32C3 引脚配置 */
// I2C引脚配置
#define BOARD_I2C_SDA_IO          GPIO_NUM_21
#define BOARD_I2C_SCL_IO          GPIO_NUM_20
// 按键GPIO配置（按钮按下时接GND）
#define BOARD_GPIO_KEY_UP     GPIO_NUM_5
#define BOARD_GPIO_KEY_DOWN   GPIO_NUM_6
#define BOARD_GPIO_KEY_ENTER  GPIO_NUM_7
#define BOARD_GPIO_KEY_BACK   GPIO_NUM_8
// 震动马达GPIO配置
#define BOARD_GPIO_VIBRATE    GPIO_NUM_10
// 电池电量估测ADC GPIO配置
#define BOARD_GPIO_BATTERY    GPIO_NUM_4
// LED GPIO配置
#define BOARD_GPIO_LED_1      GPIO_NUM_0
#define BOARD_GPIO_LED_2      GPIO_NUM_1
#define BOARD_GPIO_LED_3      GPIO_NUM_3
// 显示屏复位引脚配置
#define BOARD_GPIO_DISPLAY_RESET GPIO_NUM_2

/* ================== 内部函数声明 ================== */
void board_i2c_init(void);
void board_display_init(void);
void board_key_init(void);
void board_vibrate_init(void);
void board_leds_init(void);
void board_power_init(void);

/* ================== LED 接口声明 ================== */
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
void board_leds_tick(void);               // 状态机轮询
bool board_leds_is_active(void);          // 是否有活跃效果

/* ================== 震动接口声明 ================== */
void board_vibrate_short(void);           // 短震动
void board_vibrate_double(void);          // 震动两次
void board_vibrate_tick(void);            // 状态机轮询
bool board_vibrate_is_active(void);       // 是否有活跃震动