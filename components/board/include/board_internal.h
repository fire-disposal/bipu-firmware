#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

// I2C总线句柄，供显示模块等使用
extern i2c_master_bus_handle_t board_i2c_bus_handle;

/* ================== 内部初始化函数声明 ================== */

void board_i2c_init(void);
void board_display_init(void);
void board_key_init(void);
void board_vibrate_init(void);
void board_leds_init(void);
void board_power_init(void);