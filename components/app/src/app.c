#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "app.h"
#include "esp_log.h"
#include "esp_err.h"
#include "app_effects.h"
#include "app_battery.h"
#include "app_conn_sm.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";
/* ========== 配置常量（替代魔法数字） ========== */
#define BATTERY_UPDATE_INTERVAL_MS 60000U
#define CONNECT_BLINK_DURATION_MS 3000U
#define CONNECT_BLINK_INTERVAL_MS 200U

// GUI 任务配置（分离 UI 刷屏至低优先级任务）
#define GUI_TASK_PRIORITY    3
#define GUI_TASK_PERIOD_MS   100
#define GUI_TASK_STACK_SIZE  4096

/* ===================== BLE 消息接收回调 ===================== */
/* ===================== CTS 时间同步回调 ===================== */

#include "app_ble.h"

/* ===================== GUI 任务（在 app 组件内） ===================== */
static void gui_task(void* pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(APP_TAG, "GUI task started (period %d ms)", GUI_TASK_PERIOD_MS);
    for (;;) {
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(GUI_TASK_PERIOD_MS));
    }
}

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    ESP_LOGI(APP_TAG, "初始化应用层...");

    esp_err_t ret = ESP_OK;

    // 初始化 BLE（包括控制器、栈和广告）——非致命：若失败则降级
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(APP_TAG, "BLE 初始化失败（降级）: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(APP_TAG, "BLE 初始化成功");

        // 设置回调
        ble_manager_set_message_callback(ble_message_received);
        ble_manager_set_cts_time_callback(ble_cts_time_received);

        // 启动 BLE 广告，允许多次重试
        int adv_attempts = 0;
        while (adv_attempts < 3) {
            ret = ble_manager_start_advertising();
            if (ret == ESP_OK) break;
            ESP_LOGW(APP_TAG, "BLE 广告启动失败（尝试 %d）: %s", adv_attempts + 1, esp_err_to_name(ret));
            adv_attempts++;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (ret != ESP_OK) {
            ESP_LOGW(APP_TAG, "BLE 广告启动失败，继续启动但不广播: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGD(APP_TAG, "BLE 广告已启动");
        }
    }

    // UI 初始化（依赖 display）
    ui_init();

    // 创建 GUI 任务（在应用组件中统一管理 UI 任务）
    BaseType_t gui_ret = xTaskCreate(
        gui_task,
        "gui_task",
        GUI_TASK_STACK_SIZE,
        NULL,
        GUI_TASK_PRIORITY,
        NULL
    );
    if (gui_ret != pdPASS) {
        ESP_LOGW(APP_TAG, "创建 GUI 任务失败，UI 可能不可用");
    }

    ESP_LOGI(APP_TAG, "应用层初始化完成（可能已降级）");
    return ESP_OK;
}

/* ===================== 应用主循环 ===================== */
void app_loop(void)
{
    // 轮询按键事件
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        ESP_LOGD(APP_TAG, "Key event detected: %d", key);
        ui_on_key(key);
    }

    // BLE 轮询（处理 BLE 事件）
    ble_manager_poll();

    // 各模块的定期/短时处理
    app_effects_tick();
    // effects 优先：如果 effect 在播放，跳过 conn tick 以避免覆盖效果
    if (!app_effects_is_active()) {
        app_conn_sm_tick(ble_manager_is_connected());
    }
    app_battery_tick();
}

/* ===================== 应用清理 ===================== */
void app_cleanup(void)
{
    ESP_LOGI(APP_TAG, "清理应用层...");
    
    // 关闭 BLE 广告
    esp_err_t ret = ble_manager_stop_advertising();
    if (ret != ESP_OK) {
        ESP_LOGW(APP_TAG, "BLE 广告停止失败: %s", esp_err_to_name(ret));
    }
    
    // 关闭震动
    ret = board_vibrate_off();
    if (ret != ESP_OK) {
        ESP_LOGW(APP_TAG, "震动马达关闭失败: %s", esp_err_to_name(ret));
    }
    
    // 关闭 LED 灯
    board_leds_off();
    
    ESP_LOGI(APP_TAG, "应用层清理完成");
}
