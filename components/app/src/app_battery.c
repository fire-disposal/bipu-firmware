#include "app_battery.h"
#include "board.h"
#include "ble_manager.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_battery";

/* ========== 配置常量 ========== */
#define BATTERY_UPDATE_INTERVAL_MS  30000U  // 30秒更新一次（与 ADC 采样频率对齐）
#define BATTERY_LOG_INTERVAL_MS     300000U // 5分钟打印一次日志

static uint32_t s_last_update = 0;
static uint32_t s_last_log = 0;

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
    
    // 定期打印日志（避免过多日志）
    if (now - s_last_log >= BATTERY_LOG_INTERVAL_MS) {
        s_last_log = now;
        float battery_voltage = board_battery_voltage();
        bool is_charging = board_battery_is_charging();
        ESP_LOGI(TAG, "电池: %.2fV, %d%%, %s",
                 battery_voltage, battery_level, 
                 is_charging ? "充电中" : "未充电");
    }
}
