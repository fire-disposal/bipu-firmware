#include "board.h"
#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* MAIN_TAG = "main";

/* ======================== 任务配置 ======================== */
#define APP_TASK_STACK_SIZE      (12288)    // 支持 BLE 所需的额外栈空间
#define APP_TASK_PRIORITY        (5)
#define APP_TASK_PERIOD_MS       (50)       // 20Hz 刷新频率
#define APP_TASK_NAME            "app_task"

#define VIBRATE_STARTUP_MS       (200)
#define STARTUP_RESTART_DELAY_MS (2000)

/* ===================== 应用主任务 ===================== */
static void app_task(void* pvParameters)
{
    ESP_LOGI(MAIN_TAG, "应用任务已启动 (栈: %u 字节, 优先级: %u)",
             APP_TASK_STACK_SIZE, APP_TASK_PRIORITY);
    
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t loop_count = 0;
    uint32_t last_log_time = 0;
    
    while (1) {
        // 执行应用主循环（非阻塞）
        app_loop();
        
        // 使用 vTaskDelayUntil 保证周期执行，避免延迟累积
        if (xTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_TASK_PERIOD_MS)) == pdFAIL) {
            ESP_LOGW(MAIN_TAG, "任务周期延迟过大 (循环: %lu)", loop_count);
        }
        
        // 每 100 次循环输出一次监控日志（约5秒）
        if (++loop_count % 100 == 0) {
            uint32_t current_time = xTaskGetTickCount();
            if (current_time - last_log_time > pdMS_TO_TICKS(5000)) {
                UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
                ESP_LOGD(MAIN_TAG, "循环: %lu, 栈剩余: %u bytes", 
                         loop_count, stack_high_water * 4);
                last_log_time = current_time;
            }
        }
    }
}

/* ===================== NVS 初始化 ===================== */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(MAIN_TAG, "NVS 分区已满或版本不兼容，清除并重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(MAIN_TAG, "NVS 初始化成功");
    } else {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ===================== ESP-IDF 主入口 ===================== */
void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "==========================================");
    ESP_LOGI(MAIN_TAG, "启动 BIPI 应用 (FreeRTOS + U8G2 + BLE)");
    ESP_LOGI(MAIN_TAG, "==========================================");
    
    /* 步骤 1: 初始化 NVS（必须在BLE和其他模块前） */
    if (init_nvs() != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败，程序将在 2 秒后重启");
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    
    /* 步骤 2: 初始化硬件抽象层 */
    esp_err_t ret = board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "硬件初始化失败: %s，程序将在 2 秒后重启",
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    ESP_LOGI(MAIN_TAG, "硬件初始化成功");
    
    /* 步骤 3: 启动时震动反馈 */
    ret = board_vibrate_on(VIBRATE_STARTUP_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "震动反馈失败: %s", esp_err_to_name(ret));
    }
    
    /* 步骤 4: 初始化应用层（包含BLE初始化） */
    ret = app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "应用初始化失败: %s，程序将在 2 秒后重启",
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    ESP_LOGI(MAIN_TAG, "应用层初始化成功");
    
    /* 步骤 5: 创建应用主任务 */
    BaseType_t xReturned = xTaskCreate(
        app_task,
        APP_TASK_NAME,
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        NULL
    );
    
    if (xReturned != pdPASS) {
        ESP_LOGE(MAIN_TAG, "应用任务创建失败，程序将在 2 秒后重启");
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    
    ESP_LOGI(MAIN_TAG, "==========================================");
    ESP_LOGI(MAIN_TAG, "BIPI 应用启动成功！");
    ESP_LOGI(MAIN_TAG, "==========================================");
}