#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "app_ble";

void ble_message_received(const char* sender, const char* message)
{
    if (!sender || !message) {
        ESP_LOGW(TAG, "BLE 回调接收到无效参数");
        return;
    }

    ESP_LOGI(TAG, "BLE 消息已接收 - 发送者: %s, 内容: %s", sender, message);

    // 使用board层的统一通知接口，简化app层逻辑
    board_notify();
    
    ui_show_message(sender, message);
}
