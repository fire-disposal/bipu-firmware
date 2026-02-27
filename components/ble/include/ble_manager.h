/**
 * @file ble_manager.h
 * @brief BLE 管理器接口 (Bipupu 协议版本 1.2)
 * 
 * 基于 Nordic UART Service (NUS) 实现 Bipupu 蓝牙协议
 * 协议格式: [协议头(0xB0)][时间戳(4)][消息类型(1)][数据长度(2)][数据(N)][校验和(1)]
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 蓝牙状态定义 ================== */

/** 蓝牙管理器状态 */
typedef enum {
    BLE_STATE_UNINITIALIZED = 0,  /**< 未初始化 */
    BLE_STATE_INITIALIZED,        /**< 已初始化 */
    BLE_STATE_ADVERTISING,        /**< 正在广播 */
    BLE_STATE_CONNECTED,          /**< 已连接 */
    BLE_STATE_ERROR               /**< 错误状态 */
} ble_state_t;

/* ================== 回调函数类型定义 ================== */

/**
 * @brief 消息接收回调函数类型
 * 
 * @param sender 发送者名称 (UTF-8)
 * @param message 消息内容 (UTF-8)
 * @param timestamp Unix时间戳 (秒)
 */
typedef void (*ble_message_callback_t)(const char* sender, const char* message, uint32_t timestamp);

/**
 * @brief 时间同步回调函数类型
 * 
 * @param timestamp Unix时间戳 (秒)
 */
typedef void (*ble_time_sync_callback_t)(uint32_t timestamp);

/**
 * @brief 连接状态变化回调函数类型
 * 
 * @param connected true=已连接, false=已断开
 */
typedef void (*ble_connection_callback_t)(bool connected);

/* ================== 公共接口 ================== */

/**
 * @brief 初始化蓝牙管理器
 * 
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_init(void);

/**
 * @brief 反初始化蓝牙管理器
 * 
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_deinit(void);

/**
 * @brief 开始蓝牙广播
 * 
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_start_advertising(void);

/**
 * @brief 停止蓝牙广播
 * 
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_stop_advertising(void);

/**
 * @brief 设置消息接收回调
 * 
 * @param callback 回调函数指针
 */
void ble_manager_set_message_callback(ble_message_callback_t callback);

/**
 * @brief 设置时间同步回调
 * 
 * @param callback 回调函数指针
 */
void ble_manager_set_time_sync_callback(ble_time_sync_callback_t callback);

/**
 * @brief 设置连接状态变化回调
 * 
 * @param callback 回调函数指针
 */
void ble_manager_set_connection_callback(ble_connection_callback_t callback);

/**
 * @brief 检查蓝牙是否已连接
 * 
 * @return true 已连接
 * @return false 未连接
 */
bool ble_manager_is_connected(void);

/**
 * @brief 获取当前蓝牙状态
 * 
 * @return ble_state_t 蓝牙状态
 */
ble_state_t ble_manager_get_state(void);

/**
 * @brief 获取设备名称
 * 
 * @return const char* 设备名称字符串
 */
const char* ble_manager_get_device_name(void);

/**
 * @brief 获取错误计数
 * 
 * @return uint32_t 错误计数
 */
uint32_t ble_manager_get_error_count(void);

/**
 * @brief 轮询蓝牙管理器 (需要在主循环中调用)
 */
void ble_manager_poll(void);

/**
 * @brief 获取连接句柄
 * 
 * @return uint16_t 连接句柄，0xFFFF表示未连接
 */
uint16_t ble_manager_get_conn_id(void);

/**
 * @brief 断开当前连接
 * 
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_disconnect(void);

/**
 * @brief 发送文本消息 (设备到手机)
 * 
 * @param text 文本内容 (UTF-8)
 * @param text_length 文本长度 (字节)
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_send_text_message(const char* text, size_t text_length);

/**
 * @brief 发送时间同步响应 (设备到手机)
 * 
 * @param timestamp Unix时间戳 (秒)
 * @return esp_err_t ESP_OK 成功，其他值失败
 */
esp_err_t ble_manager_send_time_sync_response(uint32_t timestamp);

#ifdef __cplusplus
}
#endif