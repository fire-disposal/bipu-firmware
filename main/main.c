#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ui_display.h"

static const char* TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "开始Hello World示例...");

    // 启动 UI 系统（初始化并显示欢迎画面）
    if (!ui_display_start()) {
        ESP_LOGE(TAG, "UI 系统启动失败，程序终止");
        return;
    }

    // 创建 UI 渲染任务，优先级低于输入任务
    xTaskCreate(ui_render_task, "ui_render", 4096, NULL, 4, NULL);

    // 主任务进入空闲循环，避免空转占用 CPU
    vTaskDelete(NULL);  // 删除自身，释放资源
}