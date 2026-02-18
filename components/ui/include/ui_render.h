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

#ifdef __cplusplus
}
#endif
