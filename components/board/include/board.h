#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ================== 显示配置 ================== */
#define BOARD_TAG "board"
#define BOARD_I2C_MASTER_PORT     I2C_NUM_0
#define BOARD_I2C_SDA_IO          GPIO_NUM_2
#define BOARD_I2C_SCL_IO          GPIO_NUM_1
#define BOARD_I2C_FREQ_HZ         400000
#define BOARD_OLED_I2C_ADDRESS    0x3C

/* ================== GPIO配置 ================== */
// 按键GPIO配置（按钮按下时接GND）
#define BOARD_GPIO_KEY_UP     GPIO_NUM_10
#define BOARD_GPIO_KEY_DOWN   GPIO_NUM_11
#define BOARD_GPIO_KEY_ENTER  GPIO_NUM_12
#define BOARD_GPIO_KEY_BACK   GPIO_NUM_13

// 震动马达GPIO配置
#define BOARD_GPIO_VIBRATE    GPIO_NUM_4

// RGB灯GPIO配置
#define BOARD_GPIO_RGB_R      GPIO_NUM_45
#define BOARD_GPIO_RGB_G      GPIO_NUM_48
#define BOARD_GPIO_RGB_B      GPIO_NUM_47

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

// 预定义颜色宏
#define BOARD_COLOR_OFF     ((board_rgb_t){0, 0, 0})
#define BOARD_COLOR_RED     ((board_rgb_t){255, 0, 0})
#define BOARD_COLOR_GREEN   ((board_rgb_t){0, 255, 0})
#define BOARD_COLOR_BLUE    ((board_rgb_t){0, 0, 255})
#define BOARD_COLOR_YELLOW  ((board_rgb_t){255, 255, 0})
#define BOARD_COLOR_CYAN    ((board_rgb_t){0, 255, 255})
#define BOARD_COLOR_MAGENTA ((board_rgb_t){255, 0, 255})
#define BOARD_COLOR_WHITE   ((board_rgb_t){255, 255, 255})
#define BOARD_COLOR_ORANGE  ((board_rgb_t){255, 165, 0})
#define BOARD_COLOR_PURPLE  ((board_rgb_t){128, 0, 128})
#define BOARD_COLOR_PINK    ((board_rgb_t){255, 192, 203})

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

