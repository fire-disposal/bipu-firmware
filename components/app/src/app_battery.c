#include "app_battery.h"
#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_battery";

/* ========== 配置常量 ========== */
#define BATTERY_UPDATE_INTERVAL_MS  5000U  // 30秒更新一次（与 ADC 采样频率对齐）
#define BATTERY_LOG_INTERVAL_MS     30000U // 30秒打印一次日志（更频繁）
#define BATTERY_LOW_VOLTAGE_THRESHOLD 3.0f  // 低电压阈值
#define BATTERY_CRITICAL_VOLTAGE_THRESHOLD 2.8f  // 严重低电压阈值

static uint32_t s_last_update = 0;
static uint32_t s_last_log = 0;
static bool s_low_voltage_mode = false;

void app_battery_tick(void)
{
    uint32_t now = board_time_ms();
    
    // 检查是否需要更新
    if (now - s_last_update < BATTERY_UPDATE_INTERVAL_MS) {
        return;
    }
    s_last_update = now;
    
    // 获取电池状态
    uint8_t battery_level = board_battery_percent();
    float battery_voltage = board_battery_voltage();
    bool is_charging = board_battery_is_charging();
    
    // 低电压保护
    if (battery_voltage < BATTERY_CRITICAL_VOLTAGE_THRESHOLD && !is_charging) {
        // 严重低电压：关闭屏幕或大幅降低亮度
        ui_set_brightness(10);  // 最低亮度
        s_low_voltage_mode = true;
        ESP_LOGW(TAG, "严重低电压模式：%.2fV", battery_voltage);
    } else if (battery_voltage < BATTERY_LOW_VOLTAGE_THRESHOLD && !is_charging) {
        // 低电压：降低亮度
        if (!s_low_voltage_mode) {
            ui_set_brightness(50);  // 中等亮度
            s_low_voltage_mode = true;
            ESP_LOGW(TAG, "低电压模式：%.2fV", battery_voltage);
        }
    } else {
        // 电压正常：恢复正常亮度
        if (s_low_voltage_mode) {
            ui_set_brightness(100);  // 恢复默认亮度
            s_low_voltage_mode = false;
            ESP_LOGI(TAG, "电压恢复正常：%.2fV", battery_voltage);
        }
    }
    
    // 定期打印日志（避免过多日志）
    if (now - s_last_log >= BATTERY_LOG_INTERVAL_MS) {
        s_last_log = now;
        ESP_LOGI(TAG, "电池: %.2fV, %d%%, %s",
                 battery_voltage, battery_level, 
                 is_charging ? "充电中" : "未充电");
    }
}
