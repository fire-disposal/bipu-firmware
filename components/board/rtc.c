#include "board.h"      // 公共接口
#include "board_pins.h" // GPIO引脚定义和BOARD_TAG
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <time.h>

/* ================== 时间接口实现 ================== */
uint32_t board_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void board_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ================== RTC (实时时钟) 接口实现 ================== */
esp_err_t board_set_rtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                        uint8_t minute, uint8_t second) {
    // 验证输入参数
    if (year < 1900 || year > 2099 || month < 1 || month > 12 || day < 1 ||
        day > 31 || hour > 23 || minute > 59 || second > 59) {
        ESP_LOGE(BOARD_TAG, "Invalid RTC parameters: %04d-%02d-%02d %02d:%02d:%02d",
                 year, month, day, hour, minute, second);
        return ESP_ERR_INVALID_ARG;
    }

    // 创建 tm 结构体
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900; // 年份需要减去1900
    timeinfo.tm_mon = month - 1;    // 月份需要减去1 (0-11)
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = -1; // 自动判断夏令时

    // 转换为 time_t (从1970年1月1日起的秒数)
    time_t timestamp = mktime(&timeinfo);
    if (timestamp == -1) {
        ESP_LOGE(BOARD_TAG, "Failed to convert time to timestamp");
        return ESP_ERR_INVALID_ARG;
    }

    // 设置系统时间
    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;

    esp_err_t ret = settimeofday(&tv, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to set system time: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(BOARD_TAG, "RTC updated successfully: %04d-%02d-%02d %02d:%02d:%02d (timestamp=%ld)",
             year, month, day, hour, minute, second, timestamp);

    return ESP_OK;
}

esp_err_t board_set_rtc_from_timestamp(time_t timestamp) {
  struct timeval tv;
  tv.tv_sec = timestamp;
  tv.tv_usec = 0;

  esp_err_t ret = settimeofday(&tv, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(BOARD_TAG, "Failed to set system time from timestamp: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // 将时间戳转换为日期时间并打印
  struct tm *timeinfo = localtime(&timestamp);
  if (timeinfo) {
    ESP_LOGI(BOARD_TAG,
             "RTC updated from timestamp: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }

  return ESP_OK;
}