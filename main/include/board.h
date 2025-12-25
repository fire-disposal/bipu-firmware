#pragma once
#include <stdint.h>
#include <stdbool.h>

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
typedef enum {
    BOARD_RGB_OFF = 0,
    BOARD_RGB_RED,
    BOARD_RGB_GREEN,
    BOARD_RGB_BLUE,
    BOARD_RGB_YELLOW,  // RED + GREEN
    BOARD_RGB_CYAN,    // GREEN + BLUE
    BOARD_RGB_MAGENTA, // RED + BLUE
    BOARD_RGB_WHITE,   // RED + GREEN + BLUE
} board_rgb_color_t;

/* ================== 生命周期 ================== */
void board_init(void);

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
void board_vibrate_on(uint32_t ms);
void board_vibrate_off(void);
void board_vibrate_tick(void);  // 需要在主循环中调用

/* ================== RGB灯接口 ================== */
void board_rgb_init(void);
void board_rgb_set_color(board_rgb_color_t color);
void board_rgb_off(void);

/* ================== BLE接口 ================== */
void board_ble_init(void);
void board_ble_poll(void);