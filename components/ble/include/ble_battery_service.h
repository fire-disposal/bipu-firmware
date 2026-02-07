#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "ble_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 电池服务句柄 */
typedef struct {
    uint16_t service_handle;
    uint16_t level_char_handle;
} ble_battery_service_handles_t;

/**
 * @brief 初始化电池服务
 * @param gatts_if GATT 接口
 * @param handles 输出服务句柄
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_battery_service_init(esp_gatt_if_t gatts_if, ble_battery_service_handles_t* handles);

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
 * @brief 处理电池服务的事件
 * @param event 事件类型
 * @param gatts_if GATT 接口
 * @param param 事件参数
 */
void ble_battery_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

#ifdef __cplusplus
}
#endif