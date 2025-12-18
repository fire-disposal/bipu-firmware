#pragma once

// 逻辑按键事件类型
typedef enum {
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_ENTER,
    INPUT_KEY_BACK
} input_key_t;

// 对 app/ui 提供逻辑按键事件
void input_init(void);
int input_poll_key(void); // 返回 input_key_t 或 -1