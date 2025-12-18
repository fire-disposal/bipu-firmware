#pragma once
#include <stdbool.h>

typedef enum {
    BUTTON_UP = 0,
    BUTTON_DOWN,
    BUTTON_ENTER,
    BUTTON_BACK,
    BUTTON_COUNT
} button_id_t;

/**
 * 初始化按键 GPIO
 */
void buttons_init(void);

/**
 * 读取某个按键是否被按下
 * true = 按下
 * false = 松开
 */
bool buttons_is_pressed(button_id_t button);
