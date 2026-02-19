#include "board_pins.h"  // 先拿引脚宏
#include "board.h"       // 再拿函数声明

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// I2C总线句柄定义
i2c_master_bus_handle_t board_i2c_bus_handle = NULL;

// 系统清理回调
static board_cleanup_callback_t s_cleanup_callback = NULL;


/* ================== 系统清理管理 ================== */
void board_register_cleanup_callback(board_cleanup_callback_t callback) {
    s_cleanup_callback = callback;
    ESP_LOGI(BOARD_TAG, "Cleanup callback registered");
}

void board_execute_cleanup(void) {
    if (s_cleanup_callback != NULL) {
        ESP_LOGI(BOARD_TAG, "Executing cleanup callback");
        s_cleanup_callback();
    } else {
        ESP_LOGW(BOARD_TAG, "No cleanup callback registered");
    }
}

/* ================== 通用通知接口 ================== */
void board_notify(void) {
    ESP_LOGD(BOARD_TAG, "Board notify: short vibrate + LED notify");
    
    // 短震动（异步，不阻塞）
    board_vibrate_short();
    
    // 触发 LED 通知闪烁（由 board_leds_tick 处理）
    board_leds_notify();
}
