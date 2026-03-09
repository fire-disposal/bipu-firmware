#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- 协议常量 ---
#define PROTOCOL_TIME_SYNC    0xA1
#define PROTOCOL_MSG_FORWARD  0xA2

// --- BLE 状态枚举 ---
typedef enum {
    BLE_STATE_UNINITIALIZED,
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_ERROR
} ble_state_t;

// --- 消息回调函数类型 ---
typedef void (*ble_message_callback_t)(const char* sender, const char* message, uint32_t timestamp);
typedef void (*ble_time_sync_callback_t)(uint32_t timestamp);
typedef void (*ble_connection_callback_t)(bool connected);

// --- 外部接口 ---

/**
 * @brief 初始化 BLE 管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_init(void);

/**
 * @brief 设置消息接收回调函数
 * @param callback 回调函数指针
 */
void ble_manager_set_message_callback(ble_message_callback_t callback);

/**
 * @brief 检查 BLE 是否已连接
 * @return true 已连接，false 未连接
 */
bool ble_manager_is_connected(void);

/**
 * @brief 获取当前 BLE 状态
 * @return 当前状态
 */
ble_state_t ble_manager_get_state(void);

/**
 * @brief 开始 BLE 广播
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_start_advertising(void);

/**
 * @brief 初始化并启动蓝牙 NimBLE 堆栈
 * 代替直接在 app_main 中写一堆初始化代码
 */
void ble_manager_start(void);

/**
 * @brief 强制清除所有绑定信息并断开连接
 * 用于你的"本地按键解绑"逻辑
 */
void ble_manager_force_reset_bonds(void);

/**
 * @brief 发送绑定信息到设备
 * @param app_id 应用ID
 * @param user_name 用户名
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_send_binding_info(const char* app_id, const char* user_name);

/**
 * @brief 发送解绑确认到设备
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_manager_send_unbind_confirm(void);

/**
 * @brief 全局连接状态标识
 */
extern bool ble_is_connected;

/**
 * @brief 清理BLE管理器资源
 */
void ble_manager_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_MANAGER_H