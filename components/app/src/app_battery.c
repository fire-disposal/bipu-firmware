#include "app_battery.h"
#include "board.h"
#include "ble_manager.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_battery";

void app_battery_tick(void)
{
    static uint32_t last_battery_update = 0;
    uint32_t now = board_time_ms();
    if (now - last_battery_update > 10000U) {
        last_battery_update = now;
        uint8_t battery_level = board_battery_percent();
        ble_manager_update_battery_level(battery_level);

        float battery_voltage = board_battery_voltage();
        bool is_charging = board_battery_is_charging();
        ESP_LOGI(TAG, "电池状态更新 - 电压: %.2fV, 电量: %d%%, 充电状态: %s",
                 battery_voltage, battery_level, is_charging ? "充电中" : "未充电");
    }
}
