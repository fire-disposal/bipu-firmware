#include "app_battery.h"
#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>

static const char* TAG = "app_battery";

/* ========== 配置常量 ========== */
#define BATTERY_UPDATE_INTERVAL_MS  5000U  // 5秒更新一次
#define BATTERY_LOG_INTERVAL_MS     30000U // 30秒打印一次日志
#define BATTERY_LOW_VOLTAGE_THRESHOLD 3.4f  // 低电压阈值
#define BATTERY_CRITICAL_VOLTAGE_THRESHOLD 3.2f  // 严重低电压阈值

static uint32_t s_last_log = 0;
static bool s_low_voltage_mode = false;
static esp_timer_handle_t s_battery_timer = NULL;

void app_battery_tick(void)
{
    // 获取电池状态
    uint8_t battery_level = board_battery_percent();
    float battery_voltage = board_battery_voltage();
    bool is_charging = board_battery_is_charging();
    
    // 低电压保护逻辑
    if (battery_voltage < BATTERY_CRITICAL_VOLTAGE_THRESHOLD && !is_charging) {
        if (!s_low_voltage_mode) {
            ui_set_brightness(10); // 极低电量降低亮度
            s_low_voltage_mode = true;
            ESP_LOGW(TAG, "Critical Battery: %.2fV", battery_voltage);
        }
    } else if (battery_voltage < BATTERY_LOW_VOLTAGE_THRESHOLD && !is_charging) {
        if (!s_low_voltage_mode) {
            ui_set_brightness(50); // 低电量适度降低亮度
            s_low_voltage_mode = true;
            ESP_LOGW(TAG, "Low Battery: %.2fV", battery_voltage);
        }
    } else {
        if (s_low_voltage_mode) {
            ui_set_brightness(100); // 恢复亮度
            s_low_voltage_mode = false;
            ESP_LOGI(TAG, "Battery Recovered: %.2fV", battery_voltage);
        }
    }
    
    // 定期日志
    uint32_t now = board_time_ms();
    if (now - s_last_log >= BATTERY_LOG_INTERVAL_MS) {
        s_last_log = now;
        ESP_LOGI(TAG, "Battery: %.2fV, %d%%, %s",
                 battery_voltage, battery_level, 
                 is_charging ? "Charging" : "Discharging");
    }
}

static void battery_timer_cb(void* arg) {
    app_battery_tick();
}

void app_battery_init(void) {
    const esp_timer_create_args_t timer_args = {
        .callback = &battery_timer_cb,
        .name = "battery_monitor"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &s_battery_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return;
    }
    
    // 启动周期性定时器 (us)
    ret = esp_timer_start_periodic(s_battery_timer, BATTERY_UPDATE_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Battery monitor initialized (Period: %dms)", BATTERY_UPDATE_INTERVAL_MS);
}
