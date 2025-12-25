#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"  // 为了使用board_key_t类型

// UI 状态枚举 - 简化为核心状态
typedef enum {
    UI_STATE_STANDBY,      // 待机黑屏状态
    UI_STATE_MAIN,         // 主界面状态
    UI_STATE_MESSAGE,      // 消息查看状态
} UiState;

// 消息结构体 - 保持与board层一致，避免重复
typedef struct {
    char sender[16];
    char text[128];
    uint32_t timestamp;
    bool is_read;
} ui_message_t;

// 简化的主状态结构体 - 移除冗余字段
typedef struct {
    UiState state;
    ui_message_t messages[10];  // 固定数组，避免动态分配
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
} ui_state_t;

// UI 生命周期接口
void ui_init(void);

// 主循环和事件接口
void ui_tick(void);
void ui_on_key(board_key_t key);

// 消息显示接口
void ui_show_message(const char* sender, const char* text);

// 系统控制接口
void ui_enter_standby(void);
void ui_wake_up(void);

// 简化的状态访问接口
ui_state_t* ui_get_state(void);
UiState ui_get_current_state(void);

// 消息导航接口
void ui_next_message(void);
void ui_prev_message(void);