#include "board.h"
#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* MAIN_TAG = "main";

/* ======================== 任务配置 ======================== */
// 任务栈大小：支持 BLE 栈 + 日志缓冲
#define APP_TASK_STACK_SIZE      (8192)
// 任务优先级：中等优先级，低于 ESP 内部任务但高于 IDLE
#define APP_TASK_PRIORITY        (5)
#define APP_TASK_PERIOD_MS       (50)       // 20Hz 刷新频率
#define APP_TASK_NAME            "app_task"

#define VIBRATE_STARTUP_MS       (1000)
#define STARTUP_RESTART_DELAY_MS (2000)

// GUI 任务已移至 app 组件以保持模块划分清晰

/* ===================== 应用主任务 ===================== */
static void app_task(void* pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGD(MAIN_TAG, "应用任务已启动 (栈: %u 字节, 优先级: %u, 周期: %ums)",
             APP_TASK_STACK_SIZE, APP_TASK_PRIORITY, APP_TASK_PERIOD_MS);
    
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 执行应用主循环（非阻塞）
        app_loop();
        
        // 使用 vTaskDelayUntil 保证周期执行
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_TASK_PERIOD_MS));
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
        ESP_LOGD(MAIN_TAG, "NVS 初始化成功");
    } else {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ===================== ESP-IDF 主入口 ===================== */
void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "启动 BIPI 应用 (FreeRTOS + U8G2 + BLE)");

    // 统一启动序列，按阶段初始化并可选重试/降级
    esp_err_t err = ESP_OK;

    // 步骤1: NVS
    err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    // 步骤2: 硬件初始化（board_init），尝试一次，如果失败则重试一次
    err = board_init();
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "board_init 失败: %s，重试一次...", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200));
        err = board_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "硬件初始化失败: %s，程序将在 2 秒后重启", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    ESP_LOGD(MAIN_TAG, "硬件初始化成功");

    // 步骤3: 应用层初始化（可降级：若 BLE 初始化失败，可继续运行但禁用相关功能）
    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "应用初始化遇到问题: %s，继续启动但部分功能可能不可用", esp_err_to_name(err));
    } else {
        ESP_LOGD(MAIN_TAG, "应用层初始化成功");
    }

    // 步骤3.1: 系统就绪，开机震动以提示用户（在蓝牙广播前完成）
    ESP_LOGD(MAIN_TAG, "系统就绪，执行开机短震动");
    board_vibrate_short();

    // 步骤3.2: 启动应用级服务（由 app 组件负责启动 BLE 等）
    esp_err_t srv_ret = app_start_services();
    if (srv_ret != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "app_start_services returned %s", esp_err_to_name(srv_ret));
    }

    // 步骤4: 创建应用主任务（双核芯片绑 Core 1 避让 BLE，单核芯片绑 Core 0）
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        app_task,
        APP_TASK_NAME,
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        NULL,
        BOARD_APP_CPU
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(MAIN_TAG, "应用任务创建失败，程序将在 2 秒后重启");
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    // GUI 任务由 app 组件负责创建

    ESP_LOGI(MAIN_TAG, "BIPI 应用启动成功！");
}

/* GUI task moved into app component (app.c) */