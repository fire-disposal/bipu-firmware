#include "board.h"
#include "board_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

/* ================== 板级初始化 ================== */
esp_err_t board_init(void) {
  ESP_LOGI(BOARD_TAG, "Initializing board...");

  // 各模块初始化
  // 注意：部分模块可能依赖I2C或其他基础配置，需注意初始化顺序
  
  // 1. 基础总线初始化
  board_i2c_init();

  // 2. 独立外设初始化
  board_vibrate_init(); // 包含GPIO初始化
  board_rgb_init();     // 包含GPIO初始化
  board_key_init();     // 包含GPIO初始化
  board_power_init();   // 电池ADC初始化

  // 3. 依赖总线的外设初始化
  board_display_init(); // 依赖I2C

  ESP_LOGI(BOARD_TAG, "Board initialized successfully");
  return ESP_OK;
}

/* ================== 时间接口实现 ================== */
uint32_t board_time_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void board_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

/* ================== 反馈接口实现 ================== */
void board_notify(void) {
  // 震动提醒 (RGB灯由调用者管理，避免覆盖自定义光效)
  board_vibrate_on(100); // 震动100ms
  // board_rgb_set(BOARD_COLOR_BLUE); // 已移除，防止覆盖消息光效
  ESP_LOGI(BOARD_TAG, "Notification triggered");
}
