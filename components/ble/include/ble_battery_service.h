/**
 * @file ble_battery_service.h
 * @brief Battery Service 接口定义 (原生 NimBLE 版本)
 * 
 * 标准 BLE Battery Service (UUID: 0x180F)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ble_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化电池服务 (原生 NimBLE 版本)
 * @return 0 成功，其他值表示错误
 */
int ble_battery_service_init(void);

/**
 * @brief 反初始化电池服务
 */
void ble_battery_service_deinit(void);

/**
 * @brief 更新电池电量
 * @param level 电量百分比 (0-100)
 */
void ble_battery_service_update_level(uint8_t level);

/**
 * @brief 设置连接句柄
 * @param conn_handle 连接句柄
 */
void ble_battery_service_set_conn_handle(uint16_t conn_handle);

/**
 * @brief 获取 Battery Level 特征值句柄
 * @return 特征值句柄
 */
uint16_t ble_battery_service_get_level_handle(void);

#ifdef __cplusplus
}
#endif