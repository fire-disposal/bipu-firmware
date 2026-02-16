#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_types.h"

/* ================== UI 类型定义 ================== */

typedef enum {
    UI_STATE_STANDBY,      // 待机黑屏
    UI_STATE_MAIN,         // 主界面（时钟/状态）
    UI_STATE_MESSAGE_LIST, // 消息列表
    UI_STATE_MESSAGE_READ, // 消息阅读
    UI_STATE_SETTINGS,     // 设置页面
} ui_state_enum_t;

/* ================== UI 核心接口 ================== */

void ui_init(void);
void ui_tick(void);
void ui_on_key(board_key_t key);
void ui_change_page(ui_state_enum_t new_state);

/* ================== 消息数据接口 ================== */
int ui_get_message_count(void);
int ui_get_unread_count(void);
int ui_get_current_message_idx(void);
void ui_set_current_message_idx(int idx);
ui_message_t* ui_get_message_at(int idx);

/* ================== 业务接口 ================== */
void ui_show_message(const char* sender, const char* text);
void ui_delete_current_message(void);
void ui_enter_standby(void);
void ui_wake_up(void);

// 查询是否处于待机屏保状态
bool ui_is_in_standby(void);

/* ================== 手电筒接口 ================== */
bool ui_is_flashlight_on(void);
void ui_toggle_flashlight(void);

/* ================== 设置接口 ================== */
uint8_t ui_get_brightness(void);
void ui_set_brightness(uint8_t level);

/* ================== 系统控制接口 ================== */
void ui_system_restart(void);

#ifdef __cplusplus
}
#endif
