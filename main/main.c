#include "ble_manager.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "ui.h"
#include "ui_render.h"
#include "soc/rtc.h"


static const char *MAIN_TAG = "MAIN_BOOT";

/* ======================== 任务配置 ======================== */

#define STARTUP_RESTART_DELAY_MS (2000)

/* ======================== 任务句柄 ======================== */
static TaskHandle_t s_gui_task_handle = NULL;
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
  // 1. 【视觉优先】—— 剥离非核心初始化
  ESP_LOGI(MAIN_TAG, "Initializing I2C...");
  if (board_i2c_init() != ESP_OK) {
    ESP_LOGE(MAIN_TAG, "I2C initialization failed");
    // 继续启动，后续对 I2C 的调用需自行检查句柄
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing Display...");
  board_display_init(); 
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing UI...");
  ui_init();
  vTaskDelay(pdMS_TO_TICKS(500));

    // 步骤 1: NVS
    err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "NVS 初始化失败：%s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    // 步骤 2: 硬件初始化（board_init），尝试一次，如果失败则重试一次
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

    // 步骤 3: 应用层初始化（可降级：若 BLE 初始化失败，可继续运行但禁用相关功能）
    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "应用初始化遇到问题：%s，继续启动但部分功能可能不可用", esp_err_to_name(err));
    } else {
        ESP_LOGI(MAIN_TAG, "应用层初始化成功");
    }

    // 步骤 3.1: 系统就绪，短震动一次以提示用户（在蓝牙广播前完成）
    ESP_LOGI(MAIN_TAG, "系统就绪，执行开机短震动");
    board_vibrate_short();
    // 等待震动结束以保证用户能感知（阻塞短时间，通常 <= 1s）
    vTaskDelay(pdMS_TO_TICKS(200));

    // 步骤 3.2: 启动应用级服务（由 app 组件负责启动 BLE 等）
    esp_err_t srv_ret = app_start_services();
    if (srv_ret != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "app_start_services returned %s", esp_err_to_name(srv_ret));
    }

    // 步骤 4: 创建应用主任务（双核芯片绑 Core 1 避让 BLE，单核芯片绑 Core 0）
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        app_task,
        APP_TASK_NAME,
        APP_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY,
        NULL,
        BOARD_APP_CPU
    );

  // 6. 【电源稳压】在启动蓝牙大魔王前，给系统一个缓冲期
  // 此时屏幕和逻辑任务已稳定运行，电流平稳
  vTaskDelay(pdMS_TO_TICKS(500));

  // 7. 【高能耗动作】分步初始化电池管理和蓝牙
  ESP_LOGI(MAIN_TAG, "Initializing Battery Manager...");
  board_battery_manager_init();

  vTaskDelay(pdMS_TO_TICKS(500));

/* GUI task moved into app component (app.c) */
