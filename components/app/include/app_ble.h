/**
 * @file app_ble.h
 * @brief 蓝牙应用层适配头文件 (Bipupu 协议版本 1.2)
 * 
 * 定义蓝牙消息接收和连接状态变化的回调函数接口
 * 时间同步已由蓝牙层直接处理
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 蓝牙消息接收回调函数
 * 
 * @param sender 发送者名称 (UTF-8)
 * @param message 消息内容 (UTF-8)
 * @param timestamp Unix时间戳 (秒)
 */
void ble_message_received(const char* sender, const char* message, uint32_t timestamp);

/**
 * @brief 初始化 BLE 应用层消息事件队列
 *
 * 必须在向 ble_manager 注册回调之前调用（通常在 app_init 最早期）。
 * 幂等——多次调用安全。
 */
void app_ble_init(void);

/**
 * @brief 处理队列中待处理的 BLE 消息事件（在 app_task 上下文中调用）
 *
 * 应在 app_loop 中以 >= 50Hz 的频率调用。
 * 每次调用会排空当前队列中的所有待处理消息。
 */
void app_ble_process_pending(void);

/**
 * @brief 蓝牙连接状态变化回调（由 NimBLE 任务调用）
 *
 * 仅调用非阻塞操作（LED 状态机、UI 重绘请求），对 NimBLE 任务安全。
 *
 * @param connected true = 已连接 Phone，false = 已断开/广播中
 */
void ble_connection_changed(bool connected);

#ifdef __cplusplus
}
#endif