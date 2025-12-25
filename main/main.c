#include "board.h"
#include "ui.h"
#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* MAIN_TAG = "main";

// 应用主任务
static void app_task(void* pvParameters)
{
    ESP_LOGI(MAIN_TAG, "应用任务启动...");
    
    while (1) {
        app_loop(); // 调用应用层主循环
        vTaskDelay(50 / portTICK_PERIOD_MS); // 20Hz刷新频率
    }
}

// ESP-IDF 程序入口
void app_main(void) {
    ESP_LOGI(MAIN_TAG, "启动BIPI应用...");
    
    // 初始化硬件抽象层
    board_init();
    
    // 启动时震动反馈
    board_vibrate_on(200); // 震动200ms
    
    // 初始化应用层
    app_init();
    
    // 创建应用主任务
    xTaskCreate(app_task, "app_task", 4096, NULL, 5, NULL);
    
    // 主任务不需要继续执行，可以删除自身
    vTaskDelete(NULL);
}