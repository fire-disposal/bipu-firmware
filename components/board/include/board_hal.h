#pragma once
// board_hal.h 已合并到 board.h + board_pins.h，保留此文件仅为兼容性
#include "board.h"
#include "board_pins.h"

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