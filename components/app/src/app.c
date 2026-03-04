#include "app.h"
#include "app_battery.h"
#include "app_ble.h"
#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";

/* ========== 配置常量 ========== */
#define BLE_ADV_RETRY_COUNT      3
#define BLE_ADV_RETRY_DELAY_MS   200

// GUI 任务配置（分离 UI 刷屏至低优先级任务）
#define GUI_TASK_PRIORITY    (3)
#define GUI_TASK_PERIOD_MS   (50)
#define GUI_TASK_STACK_SIZE  (4096)

// 任务句柄（用于后续管理）
static TaskHandle_t s_gui_task_handle = NULL;

/* BLE 状态缓存（用于日志记录） */
static bool s_last_connected = false;
static bool s_last_advertising = false;

/* ===================== GUI 任务 ===================== */

static void ui_redraw_callback(void) {
    if (s_gui_task_handle != NULL) {
        xTaskNotifyGive(s_gui_task_handle);
    }
}

static void gui_task(void* pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(APP_TAG, "GUI 任务已启动 (事件驱动模式)");

    // 注册重绘回调
    ui_set_redraw_callback(ui_redraw_callback);

    // 初始渲染一次，获取首次休眠时间
    uint32_t sleep_ms = ui_tick();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));
        sleep_ms = ui_tick();

#ifdef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
        // 栈监控：每 30 秒打印一次副高水位标记（仅 DEBUG 构建）
        static uint32_t s_stack_check_time = 0;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - s_stack_check_time >= 30000U) {
            s_stack_check_time = now_ms;
            ESP_LOGD(APP_TAG, "[Stack] gui_task high water: %u words",
                     uxTaskGetStackHighWaterMark(NULL));
        }
#endif
    }
}

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    ESP_LOGI(APP_TAG, "初始化应用层...");

    esp_err_t ret = ESP_OK;

    // 1. 初始化 BLE（非致命：若失败则降级）
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(APP_TAG, "BLE 初始化失败（降级运行）: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(APP_TAG, "BLE 初始化成功");

        // 设置回调
        ble_manager_set_message_callback(ble_message_received);
        ble_manager_set_connection_callback(ble_connection_changed);

        // BLE 广播延后启动：由系统入口在所有初始化完成后触发
    }

    // 2. UI 初始化
    // 注意：ui_init 内部调用 storage_init + 加载持久化消息，必须在 NVS 初始化之后调用。
    // main.c 不再单独调用 ui_init，统一在此处调用一次。
    ui_init();

    // 3. 电池监控初始化（后台定时器）
    app_battery_init();

    // 4. 创建 GUI 任务（双核芯片绑 Core 1 避让 BLE，单核芯片绑 Core 0）
    BaseType_t gui_ret = xTaskCreatePinnedToCore(
        gui_task,
        "gui_task",
        GUI_TASK_STACK_SIZE,
        NULL,
        GUI_TASK_PRIORITY,
        &s_gui_task_handle,
        BOARD_APP_CPU
    );

    if (gui_ret != pdPASS) {
        ESP_LOGE(APP_TAG, "GUI 任务创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(APP_TAG, "应用层初始化完成");
    return ESP_OK;
}

static void update_ble_state_logging(void) {
    // 仅用于记录 BLE 状态变化日志
    bool connected = ble_manager_is_connected();
    bool advertising = (ble_manager_get_state() == BLE_STATE_ADVERTISING);

    // 检查状态是否变化
    if (connected != s_last_connected || advertising != s_last_advertising) {
        s_last_connected = connected;
        s_last_advertising = advertising;

        if (connected) {
            ESP_LOGD(APP_TAG, "BLE connected");
        } else if (advertising) {
            ESP_LOGD(APP_TAG, "BLE advertising");
        } else {
            ESP_LOGD(APP_TAG, "BLE idle");
        }
    }
}

/* ===================== 应用主循环 ===================== */
void app_loop(void)
{
    // 1. 按键轮询：始终高频，保证即时响应
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        ui_on_key(key);
    }

    // 2. 震动马达状态更新：始终高频（定时器精度需要）
    board_vibrate_tick();

    // 3. LED 状态机轮询：高频调用以保证动画流畅
    board_leds_tick();

    // 4. 以下非关键路径每 200ms 执行一次（5Hz）
    static uint32_t s_slow_tick_time = 0;
    uint32_t now = board_time_ms();
    if (now - s_slow_tick_time >= 200) {
        s_slow_tick_time = now;

        // BLE 事件处理（NimBLE 事件驱动，此函数仅做轻量检查）
        ble_manager_poll();

        // BLE 状态日志记录
        update_ble_state_logging();

#ifdef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
        // 栈高水位监控：不影响量产，只在 DEBUG 构建时启用
        static uint32_t s_stack_warn_time = 0;
        if (now - s_stack_warn_time >= 30000U) {
            s_stack_warn_time = now;
            ESP_LOGD(APP_TAG, "[Stack] app_task high water: %u words",
                     uxTaskGetStackHighWaterMark(NULL));
        }
#endif
    }
}

/* ===================== 应用清理 ===================== */
void app_cleanup(void)
{
    ESP_LOGI(APP_TAG, "清理应用层...");

    // 删除 GUI 任务
    if (s_gui_task_handle != NULL) {
        vTaskDelete(s_gui_task_handle);
        s_gui_task_handle = NULL;
    }

    // 关闭 BLE 广告
    ble_manager_stop_advertising();

    // 关闭外设
    board_vibrate_off();
    board_leds_off();

    ESP_LOGI(APP_TAG, "应用层清理完成");
}

// 在系统就绪后启动应用级服务（例如 BLE 广播）
esp_err_t app_start_services(void) {
    ESP_LOGI(APP_TAG, "Starting app services (post-init)");
    esp_err_t ret = ESP_OK;

    // 仅在 BLE 初始化成功的情况下尝试启动广告
    for (int i = 0; i < BLE_ADV_RETRY_COUNT; i++) {
        ret = ble_manager_start_advertising();
        if (ret == ESP_OK) {
            ESP_LOGI(APP_TAG, "BLE 广告已启动 (from app_start_services)");
            break;
        }
        ESP_LOGW(APP_TAG, "BLE 广告启动失败 (尝试 %d/%d): %s",
                 i + 1, BLE_ADV_RETRY_COUNT, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(BLE_ADV_RETRY_DELAY_MS));
    }

    return ret;
}
