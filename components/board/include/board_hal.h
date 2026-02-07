#pragma once
#include "driver/i2c_master.h"

/* ================== 配置常量 ================== */
#define BOARD_TAG "board"

//屏幕配置
#define BOARD_I2C_SDA_IO          GPIO_NUM_10
#define BOARD_I2C_SCL_IO          GPIO_NUM_20
#define BOARD_I2C_FREQ_HZ         10000
#define BOARD_OLED_I2C_ADDRESS    0x3C
extern i2c_master_bus_handle_t board_i2c_bus_handle;
// 按键GPIO配置（按钮按下时接GND）
#define BOARD_GPIO_KEY_UP     GPIO_NUM_5
#define BOARD_GPIO_KEY_DOWN   GPIO_NUM_6
#define BOARD_GPIO_KEY_ENTER  GPIO_NUM_7
#define BOARD_GPIO_KEY_BACK   GPIO_NUM_21
// 震动马达GPIO配置
#define BOARD_GPIO_VIBRATE    GPIO_NUM_4
// 电池电量GPIO配置
#define BOARD_GPIO_BATTERY    GPIO_NUM_0
// LED GPIO配置
#define BOARD_GPIO_LED_1      GPIO_NUM_1
#define BOARD_GPIO_LED_2      GPIO_NUM_2
#define BOARD_GPIO_LED_3      GPIO_NUM_3

/* ================== 内部函数声明 ================== */
void board_i2c_init(void);
void board_display_init(void);
void board_key_init(void);
void board_vibrate_init(void);
void board_leds_init(void);
void board_power_init(void);
