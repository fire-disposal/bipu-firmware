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
uint32_t ui_tick(void); // 返回下一次 tick 的等待时间 (ms)
void ui_on_key(board_key_t key);
void ui_change_page(ui_state_enum_t new_state);

/**
 * @brief 请求重绘 UI
 * 当 UI 状态发生变化需要刷新屏幕时调用
 */
void ui_request_redraw(void);

/**
 * @brief 设置重绘回调函数
 * @param cb 回调函数，通常用于唤醒 GUI 任务
 */
void ui_set_redraw_callback(void (*cb)(void));

/* ================== 消息数据接口 ================== */
int ui_get_message_count(void);
int ui_get_unread_count(void);
int ui_get_current_message_idx(void);
void ui_set_current_message_idx(int idx);
ui_message_t* ui_get_message_at(int idx);

/* ================== 业务接口 ================== */
void ui_show_message(const char* sender, const char* text);
void ui_show_message_with_timestamp(const char* sender, const char* text, uint32_t timestamp);
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

/**
 * @brief 刷新待执行的延迟 NVS 持久化操作
 *
 * ui_delete_current_message() 和 ui_set_brightness() 在 ui_on_key() 持锁时
 * 被调用，不能在锁内直接写 NVS（约 10-50ms）。它们会将待写数据快照到模块变量，
 * 然后由 app_loop() 在无锁状态下调用此函数完成实际写入。
 *
 * 必须在 app_task 上下文中、非锁内调用。
 */
void ui_flush_pending_saves(void);

/* ================== Toast / HUD 接口 ================== */
/**
 * @brief 在屏幕中央弹出文字提示（类 Android Toast）
 *
 * 任意按键可立即消除。提示覆盖在当前页面之上，不切换页面。
 *
 * @param msg          显示文本（最多 63 个字节，支持中英文）
 * @param auto_dismiss_ms 自动消失时间 ms，0 = 不自动消失（仅靠按键消除）
 */
void ui_show_toast(const char *msg, uint32_t auto_dismiss_ms);

/** @brief 当前是否有 Toast 正在显示 */
bool ui_toast_is_visible(void);

/** @brief 立即关闭 Toast（由按键或外部逻辑调用） */
void ui_toast_dismiss(void);

#ifdef __cplusplus
}
#endif
