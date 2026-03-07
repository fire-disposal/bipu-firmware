#pragma once
#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 渲染主界面
 * @param message_count 总消息数
 * @param unread_count 未读消息数
 */
void ui_render_main(int message_count, int unread_count);

/**
 * @brief 渲染消息阅读界面
 * @param msg 当前消息指针
 * @param current_idx 当前消息索引 (0-based)
 * @param total_count 总消息数
 */
// vertical_offset: 垂直偏移，单位像素，用于实现消息内容滚动
void ui_render_message_read(const ui_message_t* msg, int current_idx, int total_count, int vertical_offset);

/**
 * @brief 渲染待机界面 (清屏)
 */
void ui_render_standby(void);

/**
 * @brief 渲染开机 LOGO
 */
void ui_render_logo(void);

/**
 * @brief 在当前帧缓冲区顶层绘制 Toast 覆盖层
 *
 * 只应在 board_display_end() → SendBuffer 之前调用（即在 pre-flush 钩子中）。
 * 会在屏幕中央绘制带圆角边框的反色文字框。
 *
 * @param msg 要显示的文本（单行，中英文均可）
 */
void ui_render_toast_overlay(const char *msg);

#ifdef __cplusplus
}
#endif
