#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "app.h"


static const char *MAIN_TAG = "MAIN_BOOT";

/* ======================== 任务配置 ======================== */

#define STARTUP_RESTART_DELAY_MS (2000)

// 应用主任务配置
#define APP_TASK_NAME        "app_task"
#define APP_TASK_STACK_SIZE  (4096)
#define APP_TASK_PRIORITY    (4)
#define APP_TASK_PERIOD_MS   (10)

/* ======================== 任务句柄 ======================== */
static TaskHandle_t s_app_task_handle = NULL;

/* ===================== 应用主任务 ===================== */
static void app_task(void* pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(MAIN_TAG, "应用任务已启动 (栈：%u 字节，优先级：%u, 周期：%ums)",
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
        ESP_LOGI(MAIN_TAG, "NVS 初始化成功");
    } else {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败：%s", esp_err_to_name(ret));
    }

    return ret;
}

/* ======================== 主入口 ======================== */

void app_main(void) {
    esp_err_t err = ESP_OK;

    // ═══════════════════════════════════════════════════
    // 阶段1 【视觉优先】早期显示初始化
    // 仅启动 I2C 总线 + 显示屏，让用户尽快看到屏幕
    // ═══════════════════════════════════════════════════
    ESP_LOGI(MAIN_TAG, "Initializing I2C...");
    if (board_i2c_init() != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "I2C initialization failed");
    }
    // I2C 总线上电稳定约 5ms，无需 500ms
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(MAIN_TAG, "Initializing Display...");
    board_display_init(); // 内部含 100ms 硬件复位序列，无需额外延时

    // ═══════════════════════════════════════════════════
    // 阶段2 NVS 初始化（app_init/ui_init 需要）
    // ═══════════════════════════════════════════════════
    err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败：%s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    // ═══════════════════════════════════════════════════
    // 阶段3 完整硬件初始化
    // board_init 内部会跳过已初始化的 I2C 和 Display（重入保护）
    // ═══════════════════════════════════════════════════
    err = board_init();
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "board_init 失败：%s，重试一次...", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200));
        err = board_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "硬件初始化失败：%s，程序将在 2 秒后重启", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
    ESP_LOGI(MAIN_TAG, "硬件初始化成功");

    // ═══════════════════════════════════════════════════
    // 阶段 3.5 从 NVS 恢复系统时间（在应用初始化前）
    // 这样 UI 显示消息时可以用准确的时间戳
    // ═══════════════════════════════════════════════════
    ESP_LOGI(MAIN_TAG, "恢复系统时间...");
    board_restore_time_from_sync();

    // ═══════════════════════════════════════════════════
    // 阶段4 应用层初始化（含 ui_init + ble_init，仅调用一次）
    // ═══════════════════════════════════════════════════
    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "应用初始化遇到问题：%s，继续启动但部分功能可能不可用", esp_err_to_name(err));
    } else {
        ESP_LOGI(MAIN_TAG, "应用层初始化成功");
    }

    // 系统就绪，短震动提示用户
    ESP_LOGI(MAIN_TAG, "系统就绪，执行开机短震动");
    board_vibrate_short();
    vTaskDelay(pdMS_TO_TICKS(200));

    // ═══════════════════════════════════════════════════
    // 阶段5 启动 BLE 广播等后台服务
    // ═══════════════════════════════════════════════════
    esp_err_t srv_ret = app_start_services();
    if (srv_ret != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "app_start_services returned %s", esp_err_to_name(srv_ret));
    }

    // ═══════════════════════════════════════════════════
    // 阶段6 创建应用主任务（双核绑 Core 1 避让 BLE，单核绑 Core 0）
    // ═══════════════════════════════════════════════════
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        app_task,
        APP_TASK_NAME,
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        &s_app_task_handle,
        BOARD_APP_CPU
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(MAIN_TAG, "应用任务创建失败");
    }
}

