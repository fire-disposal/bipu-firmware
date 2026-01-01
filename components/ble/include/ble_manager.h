#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== BLE配置常量 ================== */
#define BLE_DEVICE_NAME           "BIPU_Device"
#define BLE_MAX_MESSAGE_LEN       128
#define BLE_ADV_INTERVAL_MIN      0x20
#define BLE_ADV_INTERVAL_MAX      0x40

/* ================== UUID 定义 ================== */
// Bipupu Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_SERVICE_UUID_128   {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}
// Command Input: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_CHAR_CMD_UUID_128  {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}
// Status Output: 6E400004-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_CHAR_STATUS_UUID_128 {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x04, 0x00, 0x40, 0x6E}

// Battery Service: 0x180F
#define BATTERY_SERVICE_UUID      0x180F
// Battery Level: 0x2A19
#define BATTERY_LEVEL_UUID        0x2A19

/* ================== 协议定义 ================== */
#define PROTOCOL_VERSION          0x01
#define CMD_TYPE_MESSAGE          0x01

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
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t duration_ms;
} ble_effect_t;

typedef void (*ble_message_callback_t)(const char* sender, const char* message, const ble_effect_t* effect);

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
 * @brief BLE轮询处理（用于主循环中）
 */
void ble_manager_poll(void);

#ifdef __cplusplus
}
#endif
