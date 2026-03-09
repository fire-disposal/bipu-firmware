#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_task.h
 * @brief UI 任务管理（GUI 渲染任务）
 * 
 * 职责：
 * - 管理 GUI 渲染任务的生命周期
 * - 提供重绘通知机制
 * - 独立于 app 层，UI 组件自包含
 */

/* ================== 配置常量 ================== */

/** GUI 任务优先级（低于 app_task 的 4） */
#define UI_TASK_PRIORITY       (3)

/** GUI 任务栈大小（字节） */
#define UI_TASK_STACK_SIZE     (4096)

/** GUI 任务默认刷新间隔（毫秒） */
#define UI_TASK_DEFAULT_PERIOD_MS  (50)

/* ================== 任务句柄（外部可访问） ================== */

/**
 * @brief 获取 GUI 任务句柄
 * @return TaskHandle_t 任务句柄，NULL 表示未启动
 */
TaskHandle_t ui_task_get_handle(void);

/* ================== 生命周期管理 ================== */

/**
 * @brief 启动 GUI 任务
 * 
 * 在 ui_init() 中调用，创建 GUI 渲染任务
 * 
 * @return esp_err_t 
 *   - ESP_OK: 启动成功
 *   - ESP_FAIL: 启动失败
 */
esp_err_t ui_task_start(void);

/**
 * @brief 停止 GUI 任务
 * 
 * 在 ui_deinit() 中调用，销毁 GUI 渲染任务
 */
void ui_task_stop(void);

/**
 * @brief 检查 GUI 任务是否已启动
 * @return true 已启动
 * @return false 未启动
 */
bool ui_task_is_running(void);

/* ================== 重绘通知机制 ================== */

/**
 * @brief 请求重绘 UI
 * 
 * 线程安全，可在任意任务/中断上下文中调用
 * 调用后会唤醒 GUI 任务立即执行渲染
 */
void ui_task_request_redraw(void);

/**
 * @brief 设置重绘回调函数
 * 
 * 当需要重绘时，此回调会被调用（用于唤醒 GUI 任务）
 * 
 * @param cb 回调函数指针
 */
void ui_task_set_redraw_callback(void (*cb)(void));

/**
 * @brief 获取当前刷新间隔
 * @return uint32_t 刷新间隔（毫秒）
 */
uint32_t ui_task_get_period_ms(void);

/**
 * @brief 设置下次刷新间隔
 * @param period_ms 刷新间隔（毫秒）
 */
void ui_task_set_next_period_ms(uint32_t period_ms);

#ifdef __cplusplus
}
#endif
