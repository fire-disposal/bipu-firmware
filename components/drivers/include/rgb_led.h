#pragma once

#include <stdint.h>

// RGB 灯控制接口
void rgb_led_init(void);
void rgb_led_set(uint8_t r, uint8_t g, uint8_t b);
void rgb_led_off(void);