#include "board.h"
#include "board_hal.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <time.h>


/* ================== 板级初始化 ================== */
esp_err_t board_init(void) {
  ESP_LOGI(BOARD_TAG, "Initializing board...");

  // 各模块初始化
  // 注意：部分模块可能依赖 I2C 或其他基础配置，需注意初始化顺序

  // 1. 基础总线初始化
  board_i2c_init();

  // 2. 独立外设初始化
  board_vibrate_init(); // 包含 GPIO 初始化
  board_leds_init();    // 包含 GPIO 初始化（原 RGB）
  board_key_init();     // 包含 GPIO 初始化
  board_power_init();   // 电池 ADC 初始化

  // 3. 依赖总线的外设初始化
  board_display_init(); // 依赖 I2C

  ESP_LOGI(BOARD_TAG, "Board initialized successfully");
  return ESP_OK;
}

/* ================== 时间接口实现 ================== */
uint32_t board_time_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void board_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

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
  timeinfo.tm_year = year - 1900; // 年份需要减去 1900
  timeinfo.tm_mon = month - 1;    // 月份需要减去 1 (0-11)
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;
  timeinfo.tm_isdst = -1; // 自动判断夏令时

  // 转换为 time_t (从 1970 年 1 月 1 日起的秒数)
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

  ESP_LOGI(BOARD_TAG,
           "RTC updated: %04d-%02d-%02d %02d:%02d:%02d (timestamp=%ld)", year,
           month, day, hour, minute, second, timestamp);

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

/* ================== 反馈接口实现 ================== */
void board_notify(void) {
  // 短震动提醒
  board_vibrate_short();
  ESP_LOGI(BOARD_TAG, "Notification triggered");
}
