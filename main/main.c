#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 简单的按键轮询任务
static void key_poll_task(void* pvParameters)
{
    while (1) {
        board_key_t key = board_key_poll();
        if (key != BOARD_KEY_NONE) {
            ui_on_key(key);
        }
        vTaskDelay(50 / portTICK_PERIOD_MS); // 20Hz轮询频率
    }
}

// ESP-IDF 程序入口
void app_main(void) {
    ESP_LOGI("main", "启动BIPI应用...");
    
    // 初始化硬件抽象层
    board_init();
    
    // 启动时震动反馈
    board_vibrate_on(200); // 震动200ms
    
    // 初始化UI层
    ui_init();
    
    // 创建按键轮询任务
    xTaskCreate(key_poll_task, "key_poll", 2048, NULL, 5, NULL);
    
    // 主循环 - 只负责UI刷新和震动管理
    while (1) {
        ui_tick();
        board_vibrate_tick(); // 处理震动状态
        vTaskDelay(50 / portTICK_PERIOD_MS); // 20Hz刷新频率
    }
}