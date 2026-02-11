#pragma once
#include "driver/i2c_master.h"
#include "sdkconfig.h"

/* ================== 配置常量 ================== */
#define BOARD_TAG "board"

// 应用任务运行核心：双核芯片(S3)跑 Core 1 让出 Core 0 给 BLE，单核芯片(C3)只能跑 Core 0
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define BOARD_APP_CPU   1
#else
#define BOARD_APP_CPU   0
#endif

//屏幕配置
#define BOARD_I2C_FREQ_HZ         400000
#define BOARD_OLED_I2C_ADDRESS    0x3C
extern i2c_master_bus_handle_t board_i2c_bus_handle;
// /* ESP32C3 SUPER MINI 引脚*/
// // I2C引脚配置
// #define BOARD_I2C_SDA_IO          GPIO_NUM_10
// #define BOARD_I2C_SCL_IO          GPIO_NUM_20
// // 按键GPIO配置（按钮按下时接GND）
// #define BOARD_GPIO_KEY_UP     GPIO_NUM_5
// #define BOARD_GPIO_KEY_DOWN   GPIO_NUM_6
// #define BOARD_GPIO_KEY_ENTER  GPIO_NUM_7
// #define BOARD_GPIO_KEY_BACK   GPIO_NUM_21
// // 震动马达GPIO配置
// #define BOARD_GPIO_VIBRATE    GPIO_NUM_4
// // 电池电量估测ADC GPIO配置
// #define BOARD_GPIO_BATTERY    GPIO_NUM_0
// // LED GPIO配置
// #define BOARD_GPIO_LED_1      GPIO_NUM_1
// #define BOARD_GPIO_LED_2      GPIO_NUM_2
// #define BOARD_GPIO_LED_3      GPIO_NUM_3

/* ESP32-S3 N16R8 引脚定义（面向从 C3 迁移，USB 已启用，零冲突）*/
#define BOARD_I2C_SDA_IO          GPIO_NUM_17
#define BOARD_I2C_SCL_IO          GPIO_NUM_18
#define BOARD_GPIO_KEY_UP         GPIO_NUM_5
#define BOARD_GPIO_KEY_DOWN       GPIO_NUM_6
#define BOARD_GPIO_KEY_ENTER      GPIO_NUM_7
#define BOARD_GPIO_KEY_BACK       GPIO_NUM_8
#define BOARD_GPIO_VIBRATE        GPIO_NUM_9
#define BOARD_GPIO_BATTERY        GPIO_NUM_15
#define BOARD_GPIO_LED_1          GPIO_NUM_10
#define BOARD_GPIO_LED_2          GPIO_NUM_11
#define BOARD_GPIO_LED_3          GPIO_NUM_12


/* ================== 内部函数声明 ================== */
void board_i2c_init(void);
void board_display_init(void);
void board_key_init(void);
void board_vibrate_init(void);
void board_leds_init(void);
void board_power_init(void);
