/**
 * @file app_ble.h
 * @brief 蓝牙应用层适配头文件 (Bipupu 协议版本 1.2)
 * 
 * 定义蓝牙消息接收、时间同步和连接状态变化的回调函数接口
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
 * @brief 时间同步回调函数
 * 
 * @param timestamp Unix时间戳 (秒)
 */
void ble_time_sync_received(uint32_t timestamp);

/**
 * @brief 蓝牙连接状态变化回调函数
 * 
 * @param connected true=已连接, false=已断开
 */
void ble_connection_changed(bool connected);

#ifdef __cplusplus
}
#endif