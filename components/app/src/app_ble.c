/**
 * @file app_ble.c
 * @brief 蓝牙消息接收回调
 */

#include "app_ble.h"
#include "ui.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "app_ble";

/**
 * @brief 蓝牙消息接收回调
 *
 * @param sender 发送者名称 (UTF-8)
 * @param message 消息内容 (UTF-8)
 * @param timestamp Unix时间戳 (秒)
 */
void ble_message_received(const char* sender, const char* message, uint32_t timestamp)
{
    if (!sender || !message) {
        return;
    }

    ESP_LOGI(TAG, "BLE message: %s - %s", sender, message);

    // 直接显示消息
    ui_show_message_with_timestamp(sender, message, timestamp);
}



/**
 * @brief 蓝牙连接状态变化回调
 */
void ble_connection_changed(bool connected)
{
    // 连接状态变化日志
    ESP_LOGI(TAG, "BLE %s", connected ? "connected" : "disconnected");
}
