#include "app.h"
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

/** 蓝牙连接状态变化回调 */
static void ble_connection_changed(bool connected)
{
    if (connected) {
        board_leds_set_mode(BOARD_LED_MODE_CONNECTED);
    } else {
        board_leds_set_mode(BOARD_LED_MODE_ADVERTISING);
    }
    ui_request_redraw();
}

/* ===================== GUI 任务 ===================== */

static void ui_redraw_callback(void) {
    if (s_gui_task_handle != NULL) {
        xTaskNotifyGive(s_gui_task_handle);
    }
}

static void gui_task(void* pvParameters)
{
    (void)pvParameters;

    ui_set_redraw_callback(ui_redraw_callback);
    uint32_t sleep_ms = ui_tick();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));
        sleep_ms = ui_tick();
    }
}

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    esp_err_t ret = ESP_OK;

    /* 1. 初始化 BLE */
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(APP_TAG, "BLE 初始化失败 %s", esp_err_to_name(ret));
    } else {
        ble_manager_message_queue_init();
        ble_manager_set_message_callback(ui_show_message_with_timestamp);
        ble_manager_set_connection_callback(ble_connection_changed);
    }

    /* 2. UI 初始化 */
    ui_init();

    /* 3. 创建 GUI 任务 */
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
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void update_ble_state_logging(void)
{
    bool connected = ble_manager_is_connected();
    bool advertising = (ble_manager_get_state() == BLE_STATE_ADVERTISING);

    if (connected != s_last_connected || advertising != s_last_advertising) {
        s_last_connected = connected;
        s_last_advertising = advertising;
    }
}

/* ===================== 应用主循环 ===================== */
void app_loop(void)
{
    /* 1. 按键轮询 */
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        ui_on_key(key);
    }

    /* 2. BLE 消息处理 */
    ble_manager_process_pending_messages();

    /* 3. 延迟 NVS 持久化 */
    ui_flush_pending_saves();

    /* 4. 震动马达更新 */
    board_vibrate_tick();

    /* 5. LED 状态机轮询 */
    board_leds_tick();

    /* 6. 非关键路径 (200ms) */
    static uint32_t s_slow_tick_time = 0;
    uint32_t now = board_time_ms();
    if (now - s_slow_tick_time >= 200) {
        s_slow_tick_time = now;
        ble_manager_poll();
        update_ble_state_logging();
    }
}

/* ===================== 应用清理 ===================== */
void app_cleanup(void)
{
    if (s_gui_task_handle != NULL) {
        vTaskDelete(s_gui_task_handle);
        s_gui_task_handle = NULL;
    }

    ble_manager_stop_advertising();
    board_vibrate_off();
    board_leds_off();
}

/* 在系统就绪后启动应用级服务 */
esp_err_t app_start_services(void)
{
    ble_state_t state = ble_manager_get_state();
    if (state == BLE_STATE_UNINITIALIZED || state == BLE_STATE_ERROR) {
        ESP_LOGW(APP_TAG, "BLE 未就绪 state=%d", state);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    for (int i = 0; i < BLE_ADV_RETRY_COUNT; i++) {
        ret = ble_manager_start_advertising();
        if (ret == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(BLE_ADV_RETRY_DELAY_MS));
    }

    return ret;
}
