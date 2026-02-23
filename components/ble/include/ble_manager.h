/**
 * @file ble_manager.h
 * @brief BLE 管理器接口定义 (原生 NimBLE 版本)
 * 
 * BLE 管理器负责：
 * - 初始化和管理 BLE 栈 (原生 NimBLE)
 * - 管理 NUS、电池、CTS 服务
 * - 处理连接状态
 * - 提供消息收发接口
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

/* ================== BLE 状态枚举 ================== */
typedef enum {
    BLE_STATE_UNINITIALIZED,    // 未初始化
    BLE_STATE_INITIALIZING,     // 初始化中
    BLE_STATE_INITIALIZED,      // 已初始化
    BLE_STATE_ADVERTISING,      // 广播中
    BLE_STATE_CONNECTED,        // 已连接
    BLE_STATE_ERROR             // 错误状态
} ble_state_t;

/* ================== 回调函数类型 ================== */

/**
 * @brief 消息接收回调 (从 NUS TX 特征接收)
 * @param sender 发送者名称
 * @param message 消息内容
 */
typedef void (*ble_message_callback_t)(const char* sender, const char* message);

/**
 * @brief CTS 时间同步回调
 * @param cts_time CTS 时间数据
 */
typedef void (*ble_cts_time_callback_t)(const ble_cts_time_t* cts_time);

/* ================== 生命周期接口 ================== */

/**
 * @brief 初始化 BLE 管理器
 * @return ESP_OK 成功
 */
esp_err_t ble_manager_init(void);

/**
 * @brief 反初始化 BLE 管理器
 * @return ESP_OK 成功
 */
esp_err_t ble_manager_deinit(void);

/* ================== 广播接口 ================== */

/**
 * @brief 启动 BLE 广播
 * @return ESP_OK 成功
 */
esp_err_t ble_manager_start_advertising(void);

/**
 * @brief 停止 BLE 广播
 * @return ESP_OK 成功
 */
esp_err_t ble_manager_stop_advertising(void);

/* ================== 回调设置 ================== */

/**
 * @brief 设置消息接收回调
 * @param callback 回调函数
 */
void ble_manager_set_message_callback(ble_message_callback_t callback);

/**
 * @brief 设置 CTS 时间同步回调
 * @param callback 回调函数
 */
void ble_manager_set_cts_time_callback(ble_cts_time_callback_t callback);

/**
 * @brief 清除所有绑定信息并移除已保存的配对地址
 * @return ESP_OK 成功
 */
esp_err_t ble_manager_unbind(void);

/* ================== 状态查询 ================== */

/**
 * @brief 检查是否已连接
 * @return true 已连接
 */
bool ble_manager_is_connected(void);

/**
 * @brief 获取当前状态
 * @return 当前 BLE 状态
 */
ble_state_t ble_manager_get_state(void);

/**
 * @brief 获取状态字符串
 * @param state BLE 状态
 * @return 状态描述字符串
 */
const char* ble_manager_state_to_string(ble_state_t state);

/**
 * @brief 获取设备名称
 * @return 设备名称
 */
const char* ble_manager_get_device_name(void);

/**
 * @brief 获取错误计数
 * @return 错误次数
 */
uint32_t ble_manager_get_error_count(void);

/* ================== 数据更新 ================== */

/**
 * @brief BLE 轮询处理
 */
void ble_manager_poll(void);

/**
 * @brief 获取当前连接句柄
 * @return 连接句柄 (BLE_HS_CONN_HANDLE_NONE 表示未连接)
 */
uint16_t ble_manager_get_conn_id(void);

#ifdef __cplusplus
}
#endif
