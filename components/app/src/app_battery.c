#include "app_battery.h"
#include "board.h"
#include "ui.h"
#include "esp_log.h"

static const char* TAG = "app_battery";

// 低电压保护回调函数
static void low_voltage_callback(float voltage, bool is_charging) {
    ESP_LOGW(TAG, "Low voltage detected: %.2fV, charging: %s", voltage, is_charging ? "yes" : "no");
    ui_set_brightness(30); // 低电量降低亮度
}

void app_battery_init(void) {
    // 设置硬件层低电压保护回调
    board_power_set_low_voltage_callback(low_voltage_callback);
    board_power_enable_auto_protection(true);
    
    ESP_LOGI(TAG, "Battery protection initialized");
}
