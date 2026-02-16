#include "board_pins.h"  // 先拿引脚宏
#include "board.h"       // 再拿函数声明
#include "board_internal.h"  // 私有初始化函数声明

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// I2C总线句柄定义
i2c_master_bus_handle_t board_i2c_bus_handle = NULL;

/* ================== 板级初始化 ================== */
esp_err_t board_init(void) {
  ESP_LOGI(BOARD_TAG, "Initializing board...");

  // 各模块初始化
  // 注意：部分模块可能依赖I2C或其他基础配置，需注意初始化顺序

  // 1. 基础总线初始化
  board_i2c_init();

  // 2. 独立外设初始化
  board_vibrate_init(); // 包含GPIO初始化
  board_leds_init();    // 包含GPIO初始化（原 RGB）
  board_key_init();     // 包含GPIO初始化
  board_power_init();   // 电池ADC初始化

  // 3. 依赖总线的外设初始化
  board_display_init(); // 依赖I2C

  ESP_LOGI(BOARD_TAG, "Board initialized successfully");
  return ESP_OK;
}

/* ================== 通用通知接口 ================== */
void board_notify(void) {
    ESP_LOGD(BOARD_TAG, "Board notify: short vibrate + LED notify");
    
    // 短震动（异步，不阻塞）
    board_vibrate_short();
    
    // 触发 LED 通知闪烁（由 board_leds_tick 处理）
    board_leds_notify();
}
