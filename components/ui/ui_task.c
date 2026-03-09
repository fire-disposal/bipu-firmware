#include "ui_task.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ui_task";

/* ================== 模块状态 ================== */

static TaskHandle_t s_ui_task_handle = NULL;
static bool s_ui_task_running = false;
static void (*s_redraw_callback)(void) = NULL;
static uint32_t s_next_period_ms = UI_TASK_DEFAULT_PERIOD_MS;

/* ================== GUI 任务实现 ================== */

static void ui_task_entry(void* pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "GUI task started (priority=%d, stack=%d)", 
             UI_TASK_PRIORITY, UI_TASK_STACK_SIZE);
    
    // 初始延迟
    uint32_t sleep_ms = ui_tick();
    
    while (1) {
        // 等待重绘通知或超时
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));
        
        // 执行 UI tick（返回下次刷新间隔）
        sleep_ms = ui_tick();
        
        // 应用动态间隔
        if (sleep_ms == 0) {
            sleep_ms = s_next_period_ms;
        }
        s_next_period_ms = UI_TASK_DEFAULT_PERIOD_MS;  // 重置为默认值
    }
}

/* ================== 公开 API 实现 ================== */

TaskHandle_t ui_task_get_handle(void)
{
    return s_ui_task_handle;
}

esp_err_t ui_task_start(void)
{
    if (s_ui_task_running) {
        ESP_LOGW(TAG, "UI task already running");
        return ESP_OK;
    }
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        ui_task_entry,
        "ui_task",
        UI_TASK_STACK_SIZE,
        NULL,
        UI_TASK_PRIORITY,
        &s_ui_task_handle,
        0  // 绑定到 Core 0（与 BLE 同核，减少上下文切换）
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }
    
    s_ui_task_running = true;
    ESP_LOGI(TAG, "UI task created successfully");
    return ESP_OK;
}

void ui_task_stop(void)
{
    if (s_ui_task_handle != NULL) {
        vTaskDelete(s_ui_task_handle);
        s_ui_task_handle = NULL;
        s_ui_task_running = false;
        ESP_LOGI(TAG, "UI task stopped");
    }
}

bool ui_task_is_running(void)
{
    return s_ui_task_running && s_ui_task_handle != NULL;
}

void ui_task_request_redraw(void)
{
    if (s_redraw_callback != NULL) {
        s_redraw_callback();
    }
    
    if (s_ui_task_handle != NULL) {
        xTaskNotifyGive(s_ui_task_handle);
    }
}

void ui_task_set_redraw_callback(void (*cb)(void))
{
    s_redraw_callback = cb;
}

uint32_t ui_task_get_period_ms(void)
{
    return s_next_period_ms;
}

void ui_task_set_next_period_ms(uint32_t period_ms)
{
    s_next_period_ms = period_ms;
}
