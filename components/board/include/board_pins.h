#pragma once
#include "driver/gpio.h"
#include "sdkconfig.h"

// 自动根据芯片型号选择引脚配置
#if CONFIG_IDF_TARGET_ESP32C3

    #define BOARD_I2C_SDA_IO          GPIO_NUM_21
    #define BOARD_I2C_SCL_IO          GPIO_NUM_20
    #define BOARD_GPIO_KEY_UP         GPIO_NUM_5
    #define BOARD_GPIO_KEY_DOWN       GPIO_NUM_6
    #define BOARD_GPIO_KEY_ENTER      GPIO_NUM_7
    #define BOARD_GPIO_KEY_BACK       GPIO_NUM_8
    #define BOARD_GPIO_VIBRATE        GPIO_NUM_10
    #define BOARD_GPIO_BATTERY        GPIO_NUM_4
    #define BOARD_GPIO_LED_1          GPIO_NUM_0
    #define BOARD_GPIO_LED_2          GPIO_NUM_1
    #define BOARD_GPIO_LED_3          GPIO_NUM_3

#elif CONFIG_IDF_TARGET_ESP32S3

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

#endif

// 其他通用常量
#define BOARD_I2C_FREQ_HZ         100000
#define BOARD_OLED_I2C_ADDRESS    0x3C

// 应用任务运行核心：双核芯片(S3)跑 Core 1 让出 Core 0 给 BLE，单核芯片(C3)只能跑 Core 0
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define BOARD_APP_CPU   1
#else
#define BOARD_APP_CPU   0
#endif

#define BOARD_TAG "board"