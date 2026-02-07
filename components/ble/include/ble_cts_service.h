#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "ble_config.h"
#include "ble_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CTS 服务句柄 */
typedef struct {
    uint16_t service_handle;
    uint16_t time_char_handle;
    uint16_t local_time_char_handle;
} ble_cts_service_handles_t;

/* CTS 时间回调类型 */
typedef void (*ble_cts_time_callback_t)(const ble_cts_time_t* cts_time);

/**
 * @brief 初始化 CTS 服务
 * @param gatts_if GATT 接口
 * @param handles 输出服务句柄
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_cts_service_init(esp_gatt_if_t gatts_if, ble_cts_service_handles_t* handles);

/**
 * @brief 反初始化 CTS 服务
 */
void ble_cts_service_deinit(void);

/**
 * @brief 设置 CTS 时间回调
 * @param callback 回调函数
 */
void ble_cts_service_set_time_callback(ble_cts_time_callback_t callback);

/**
 * @brief 处理 CTS 服务的事件
 * @param event 事件类型
 * @param gatts_if GATT 接口
 * @param param 事件参数
 */
void ble_cts_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

#ifdef __cplusplus
}
#endif