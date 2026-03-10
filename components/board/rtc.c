#include "board.h"      // 公共接口
#include "board_pins.h" // GPIO引脚定义和BOARD_TAG
#include "storage.h"    // NVS 时间同步数据
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <time.h>

/* ================== 启动时恢复时间 ================== */

void board_restore_time_from_sync(void) {
    uint32_t last_sync_ts = 0;
    uint64_t last_sync_us = 0;

    esp_err_t err = storage_load_time_sync(&last_sync_ts, &last_sync_us);

    // 无同步记录（首次启动）
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(BOARD_TAG, "No time sync record found, system time initialized to Epoch");
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGW(BOARD_TAG, "Failed to load time sync data: %s, using Epoch", esp_err_to_name(err));
        return;
    }

    // 计算时间增量
    uint64_t current_us = esp_timer_get_time();
    int64_t delta_us = (int64_t)(current_us - last_sync_us);
    int32_t delta_sec = (int32_t)(delta_us / 1000000);

    // 还原系统时间 = 上次同步时间 + 系统运行增量
    time_t restored_time = (time_t)last_sync_ts + delta_sec;

    struct timeval tv;
    tv.tv_sec = restored_time;
    tv.tv_usec = 0;

    esp_err_t ret = settimeofday(&tv, NULL);
    if (ret == ESP_OK) {
        struct tm *timeinfo = localtime(&restored_time);
        if (timeinfo) {
            ESP_LOGI(BOARD_TAG, "Time restored from sync record: %04d-%02d-%02d %02d:%02d:%02d (delta=%lds)",
                     timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                     timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, (long)delta_sec);
        }
    } else {
        ESP_LOGW(BOARD_TAG, "Failed to restore system time: %s", esp_err_to_name(ret));
    }
}