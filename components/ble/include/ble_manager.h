#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "ble_config.h"
#include "ble_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== BLE状态枚举 ================== */
typedef enum {
    BLE_STATE_UNINITIALIZED,    // 未初始化
    BLE_STATE_INITIALIZING,     // 初始化中
    BLE_STATE_INITIALIZED,      // 已初始化
    BLE_STATE_ADVERTISING,      // 广播中
    BLE_STATE_CONNECTED,        // 已连接
    BLE_STATE_ERROR             // 错误状态
} ble_state_t;

/* BLE消息接收回调函数类型 */
typedef void (*ble_message_callback_t)(const char* sender, const char* message, const ble_effect_t* effect);

/* CTS 时间同步回调函数类型 (蓝牙标准 Current Time Service) */
typedef void (*ble_cts_time_callback_t)(const ble_cts_time_t* cts_time);

/* ================== BLE管理接口 ================== */

/**
 * @brief 初始化BLE管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_init(void);

/**
 * @brief 反初始化BLE管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_deinit(void);

/**
 * @brief 启动BLE广告
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_start_advertising(void);

/**
 * @brief 更新电池电量
 * @param level 电量百分比 (0-100)
 */
void ble_manager_update_battery_level(uint8_t level);

/**
 * @brief 停止BLE广告
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_stop_advertising(void);

/**
 * @brief 设置消息接收回调
 * @param callback 回调函数指针，为NULL时取消回调
 */
void ble_manager_set_message_callback(ble_message_callback_t callback);

/**
 * @brief 设置 CTS 时间同步回调 (蓝牙标准 Current Time Service)
 * @param callback 回调函数指针，为NULL时取消回调
 */
void ble_manager_set_cts_time_callback(ble_cts_time_callback_t callback);

/**
 * @brief 检查BLE是否已连接
 * @return true 已连接，false 未连接
 */
bool ble_manager_is_connected(void);

/**
 * @brief 获取BLE当前状态
 * @return 当前BLE状态
 */
ble_state_t ble_manager_get_state(void);

/**
 * @brief 获取BLE状态字符串
 * @param state BLE状态
 * @return 状态字符串
 */
const char* ble_manager_state_to_string(ble_state_t state);

/**
 * @brief 获取BLE设备名称
 * @return 设备名称
 */
const char* ble_manager_get_device_name(void);

/**
 * @brief 获取BLE错误计数
 * @return 错误总次数
 */
uint32_t ble_manager_get_error_count(void);

/**
 * @brief 主动请求CTS时间同步
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_request_cts_time_sync(void);

/**
 * @brief BLE轮询处理（用于主循环中）
 */
void ble_manager_poll(void);

#ifdef __cplusplus
}
#endif
