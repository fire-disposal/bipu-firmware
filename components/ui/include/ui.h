#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_types.h"
#include "pages/page_base.h"
#include "ui_state_machine.h"

/* ================== UI 核心接口 ================== */

/**
 * @brief UI 初始化
 * 在系统启动时调用一次
 */
void ui_init(void);

/**
 * @brief UI 周期 tick（由 GUI 任务调用）
 * @return 下次 tick 的等待时间 (ms)
 */
uint32_t ui_tick(void);

/**
 * @brief 按键处理（由 app_task 调用）
 * @param key 按键值
 */
void ui_on_key(board_key_t key);

/* ================== 页面获取接口 ================== */

/**
 * @brief 获取主页面对象
 */
ui_page_base_t* ui_get_main_page(void);

/**
 * @brief 获取列表页面对象
 */
ui_page_base_t* ui_get_list_page(void);

/**
 * @brief 获取消息页面对象
 */
ui_page_base_t* ui_get_message_page(void);

/**
 * @brief 获取设置页面对象
 */
ui_page_base_t* ui_get_settings_page(void);

/* ================== 导航接口 ================== */

/**
 * @brief 导航到新页面
 * @param page 页面对象
 * @param params 参数（可选）
 */
void ui_navigate_to_page(ui_page_base_t* page, void* params);

/**
 * @brief 返回上一页
 */
void ui_go_back_page(void);

/**
 * @brief 请求重绘 UI
 * 线程安全，可在任意任务/中断上下文中调用
 */
void ui_request_redraw(void);

/* ================== 消息数据接口 ================== */

/**
 * @brief 获取消息总数
 */
int ui_get_message_count(void);

/**
 * @brief 获取未读消息数
 */
int ui_get_unread_count(void);

/**
 * @brief 获取当前消息索引
 */
int ui_get_current_message_idx(void);

/**
 * @brief 设置当前消息索引
 * @param idx 索引值
 */
void ui_set_current_message_idx(int idx);

/**
 * @brief 获取指定索引的消息
 * @param idx 消息索引
 * @return 消息指针，NULL 表示越界
 */
ui_message_t* ui_get_message_at(int idx);

/* ================== 消息业务接口 ================== */

/**
 * @brief 显示新消息
 * @param sender 发送者
 * @param text 消息内容
 */
void ui_show_message(const char* sender, const char* text);

/**
 * @brief 显示新消息（带时间戳）
 * @param sender 发送者
 * @param text 消息内容
 * @param timestamp 时间戳
 */
void ui_show_message_with_timestamp(const char* sender, const char* text, uint32_t timestamp);

/**
 * @brief 删除当前消息
 */
void ui_delete_current_message(void);

/* ================== 手电筒接口 ================== */

/**
 * @brief 查询手电筒状态
 * @return true 已开启，false 已关闭
 */
bool ui_is_flashlight_on(void);

/**
 * @brief 切换手电筒状态
 */
void ui_toggle_flashlight(void);

/* ================== 亮度控制 ================== */

/**
 * @brief 获取当前亮度
 * @return 亮度百分比 (10-100)
 */
uint8_t ui_get_brightness(void);

/**
 * @brief 设置亮度
 * @param level 亮度百分比 (10-100)
 */
void ui_set_brightness(uint8_t level);

/* ================== 系统控制 ================== */

/**
 * @brief 系统重启
 */
void ui_system_restart(void);

/**
 * @brief 获取最后活动时间戳
 * @return 时间戳（毫秒）
 */
uint32_t ui_get_last_activity_time(void);

/**
 * @brief 刷新待执行的延迟 NVS 持久化
 * 必须在 app_task 上下文中、非锁内调用
 * 带有频率限制（2 秒间隔），避免频繁写入
 */
void ui_flush_pending_saves(void);

/**
 * @brief 强制立即保存所有待存 NVS 数据
 * 在页面切换/退出等关键时刻调用，确保数据不丢失
 */
void ui_flush_pending_saves_force(void);

/**
 * @brief 请求 NVS 保存（标记待保存）
 * 通常由页面层调用，实际写入由 ui_flush_pending_saves 执行
 */
void ui_request_nvs_save(void);

/**
 * @brief 查询是否有待保存的 NVS 数据
 * @return true 有待保存数据
 */
bool ui_has_pending_saves(void);

/**
 * @brief BLE 重连后恢复 UI 状态
 * 由 ble_manager 调用，恢复同步状态
 */
void ui_on_ble_reconnected(void);

/* ================== Toast 提示 ================== */

/**
 * @brief 显示 Toast 提示
 * @param msg 显示文本
 * @param auto_dismiss_ms 自动消失时间 (ms)，0=不自动消失
 */
void ui_show_toast(const char *msg, uint32_t auto_dismiss_ms);

/**
 * @brief 查询 Toast 是否可见
 */
bool ui_toast_is_visible(void);

/**
 * @brief 关闭 Toast
 */
void ui_toast_dismiss(void);

#ifdef __cplusplus
}
#endif
