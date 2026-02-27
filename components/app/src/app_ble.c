/**
 * @file app_ble.c
 * @brief 蓝牙应用层适配 (Bipupu 协议版本 1.2)
 * 
 * 处理蓝牙消息接收和时间同步，适配新的Bipupu协议
 */

#include "app_ble.h"
#include "board.h"
#include "ui.h"
#include "storage.h"
#include "esp_log.h"
#include "esp_err.h"
#include <time.h>

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
        ESP_LOGW(TAG, "BLE 回调接收到无效参数");
        return;
    }

    ESP_LOGI(TAG, "BLE 消息已接收 - 发送者：%s, 内容：%s, 时间戳：%u", 
             sender, message, timestamp);

    // 创建消息结构
    ui_message_t ui_msg;
    memset(&ui_msg, 0, sizeof(ui_message_t));
    
    // 复制发送者名称
    strncpy(ui_msg.sender, sender, sizeof(ui_msg.sender) - 1);
    ui_msg.sender[sizeof(ui_msg.sender) - 1] = '\0';
    
    // 复制消息内容
    strncpy(ui_msg.text, message, sizeof(ui_msg.text) - 1);
    ui_msg.text[sizeof(ui_msg.text) - 1] = '\0';
    
    // 设置时间戳
    ui_msg.timestamp = timestamp;
    ui_msg.is_read = false;
    
    // 显示消息（使用传入的时间戳）
    ui_show_message_with_timestamp(ui_msg.sender, ui_msg.text, timestamp);
    
    // 保存消息到存储
    // 注意：这里需要从UI获取当前消息列表，添加新消息，然后保存
    // 由于存储接口需要完整的消息数组，这里暂时只显示，存储逻辑在UI层处理
}

/**
 * @brief 时间同步回调
 * 
 * @param timestamp Unix时间戳 (秒)
 */
void ble_time_sync_received(uint32_t timestamp)
{
    ESP_LOGI(TAG, "时间同步已接收 - Unix时间戳：%u", timestamp);
    
    // 将Unix时间戳转换为年月日时分秒
    time_t time_val = (time_t)timestamp;
    struct tm *timeinfo = localtime(&time_val);
    
    if (!timeinfo) {
        ESP_LOGE(TAG, "时间戳转换失败");
        return;
    }
    
    ESP_LOGI(TAG, "转换后时间：%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    // 设置RTC时间
    esp_err_t ret = board_set_rtc(
        timeinfo->tm_year + 1900, 
        timeinfo->tm_mon + 1, 
        timeinfo->tm_mday,
        timeinfo->tm_hour, 
        timeinfo->tm_min, 
        timeinfo->tm_sec
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC 已成功更新");
        board_notify();  // 通知用户时间已更新
    } else {
        ESP_LOGE(TAG, "RTC 更新失败：%s", esp_err_to_name(ret));
    }
}

/**
 * @brief 蓝牙连接状态变化回调
 * 
 * @param connected true=已连接, false=已断开
 */
void ble_connection_changed(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "蓝牙设备已连接");
        // 可以在这里添加连接成功后的处理，如发送设备信息等
    } else {
        ESP_LOGI(TAG, "蓝牙设备已断开");
        // 可以在这里添加断开连接后的处理
    }
}