/**
 * @file ble_nus_service.h
 * @brief Nordic UART Service (NUS) 接口定义 (原生 NimBLE 版本)
 * 
 * NUS 服务用于实现类 UART 的双向通信：
 * - TX 特征: 手机写入数据到设备 (Write Without Response)
 * - RX 特征: 设备发送数据到手机 (Notify)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ble_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 回调函数类型 ================== */

/**
 * @brief 消息接收回调
 * @param message UTF-8 编码的消息文本
 * @param len 消息长度
 */
typedef void (*ble_nus_message_callback_t)(const char* message, uint16_t len);

/* ================== 服务接口 ================== */

/**
 * @brief 初始化 NUS 服务 (原生 NimBLE 版本)
 * @return 0 成功，其他值表示错误
 */
int ble_nus_service_init(void);

/**
 * @brief 反初始化 NUS 服务
 */
void ble_nus_service_deinit(void);

/**
 * @brief 设置消息接收回调
 * @param callback 回调函数
 */
void ble_nus_service_set_callback(ble_nus_message_callback_t callback);

/**
 * @brief 通过 RX 特征发送数据到手机
 * @param conn_handle 连接句柄
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功
 */
esp_err_t ble_nus_service_send(uint16_t conn_handle, const uint8_t* data, uint16_t len);

/**
 * @brief 获取 RX 特征值句柄 (用于发送通知)
 * @return 特征值句柄
 */
uint16_t ble_nus_service_get_rx_handle(void);

#ifdef __cplusplus
}
#endif
