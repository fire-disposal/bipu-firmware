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

/* Bipupu 服务句柄 */
typedef struct {
    uint16_t service_handle;
    uint16_t cmd_char_handle;
    uint16_t status_char_handle;
} ble_bipupu_service_handles_t;

/* 消息回调类型 */
typedef void (*ble_bipupu_message_callback_t)(const char* sender, const char* message, const ble_effect_t* effect);

/**
 * @brief 初始化 Bipupu 服务
 * @param gatts_if GATT 接口
 * @param handles 输出服务句柄
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ble_bipupu_service_init(esp_gatt_if_t gatts_if, ble_bipupu_service_handles_t* handles);

/**
 * @brief 反初始化 Bipupu 服务
 */
void ble_bipupu_service_deinit(void);

/**
 * @brief 设置消息回调
 * @param callback 回调函数
 */
void ble_bipupu_service_set_message_callback(ble_bipupu_message_callback_t callback);

/**
 * @brief 处理 Bipupu 服务的事件
 * @param event 事件类型
 * @param gatts_if GATT 接口
 * @param param 事件参数
 */
void ble_bipupu_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

#ifdef __cplusplus
}
#endif