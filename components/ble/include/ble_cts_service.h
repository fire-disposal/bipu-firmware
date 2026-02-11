/**
 * @file ble_cts_service.h
 * @brief Current Time Service (CTS) 接口定义 (原生 NimBLE 版本)
 * 
 * CTS 是蓝牙标准服务 (UUID: 0x1805)，用于时间同步。
 * - Current Time 特征 (0x2A2B): 读写，用于接收/发送时间
 * - Local Time Info 特征 (0x2A0F): 只读，提供时区信息
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ble_config.h"
#include "ble_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 回调函数类型 ================== */
typedef void (*ble_cts_time_callback_t)(const ble_cts_time_t* cts_time);

/* ================== 服务接口 ================== */

/**
 * @brief 初始化 CTS 服务 (原生 NimBLE 版本)
 * @return 0 成功
 */
int ble_cts_service_init(void);

/**
 * @brief 反初始化 CTS 服务
 */
void ble_cts_service_deinit(void);

/**
 * @brief 设置 CTS 时间回调
 */
void ble_cts_service_set_time_callback(ble_cts_time_callback_t callback);

/**
 * @brief 通知客户端当前时间
 */
esp_err_t ble_cts_service_notify_time(uint16_t conn_handle, const ble_cts_time_t* time_info);

/**
 * @brief 设置连接句柄
 */
void ble_cts_service_set_conn_handle(uint16_t conn_handle);

/**
 * @brief 获取 Current Time 特征值句柄
 */
uint16_t ble_cts_service_get_time_handle(void);

#ifdef __cplusplus
}
#endif