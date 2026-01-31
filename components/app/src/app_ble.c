#include "app_ble.h"
#include "app_effects.h"
#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "app_ble";

void ble_message_received(const char* sender, const char* message, const ble_effect_t* effect)
{
    if (!sender || !message) {
        ESP_LOGW(TAG, "BLE 回调接收到无效参数");
        return;
    }

    ESP_LOGI(TAG, "BLE 消息已接收 - 发送者: %s, 内容: %s", sender, message);

    if (effect) {
        app_effects_apply(effect);
    }

    ui_show_message(sender, message);
}

void ble_cts_time_received(const ble_cts_time_t* cts_time)
{
    if (!cts_time) {
        ESP_LOGW(TAG, "CTS 回调接收到无效参数");
        return;
    }

    ESP_LOGI(TAG, "CTS 时间已接收 - %04d-%02d-%02d %02d:%02d:%02d (weekday=%d)",
             cts_time->year, cts_time->month, cts_time->day,
             cts_time->hour, cts_time->minute, cts_time->second, cts_time->weekday);

    esp_err_t ret = board_set_rtc(cts_time->year, cts_time->month, cts_time->day,
                                  cts_time->hour, cts_time->minute, cts_time->second);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC 已成功更新");
        board_notify();
    } else {
        ESP_LOGE(TAG, "RTC 更新失败: %s", esp_err_to_name(ret));
    }
}
