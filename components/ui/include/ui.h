#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== UI 类型定义 ================== */

typedef enum {
    UI_STATE_STANDBY,      // 待机黑屏
    UI_STATE_MAIN,         // 主界面（时钟/状态）
    UI_STATE_MESSAGE_READ, // 消息阅读
} ui_state_enum_t;

/* ================== UI 核心接口 ================== */

/**
 * @brief 初始化UI组件
 */
void ui_init(void);

/**
 * @brief UI主循环tick，需要定期调用
 */
void ui_tick(void);

/**
 * @brief 处理按键事件
 * @param key 按键键值
 */
void ui_on_key(board_key_t key);

/* ================== 消息接口 ================== */

/**
 * @brief 显示新消息（会自动唤醒屏幕并跳转到消息页）
 * @param sender 发送者名称
 * @param text 消息内容
 */
void ui_show_message(const char* sender, const char* text);

/* ================== 控制接口 ================== */

/**
 * @brief 进入待机模式
 */
void ui_enter_standby(void);

/**
 * @brief 唤醒屏幕
 */
void ui_wake_up(void);

#ifdef __cplusplus
}
#endif
