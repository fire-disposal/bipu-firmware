#include "app.h"
#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "sleep.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";

/* ========== 配置常量 ========== */
#define BLE_ADV_RETRY_COUNT      3
#define BLE_ADV_RETRY_DELAY_MS   200

/* BLE 状态缓存（用于日志记录） */
static bool s_last_connected = false;
static bool s_last_advertising = false;

/** BLE 重连后恢复回调 */
static void ble_reconnected(void)
{
    ESP_LOGI(APP_TAG, "BLE reconnected, resyncing UI state");
    /* 通知 UI 层恢复状态 */
}

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

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    esp_err_t ret = ESP_OK;

    /* 0. 初始化电源管理模块 */
    board_power_mgmt_init();

    /* 1. 初始化 BLE（核心业务） */
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(APP_TAG, "BLE 初始化失败：%s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(APP_TAG, "BLE 初始化成功");
        ble_manager_message_queue_init();
        ble_manager_set_message_callback(ui_show_message_with_timestamp);
        ble_manager_set_connection_callback(ble_connection_changed);
        ble_manager_set_reconnect_callback(ble_reconnected);
    }

    /* 2. UI 初始化 */
    ui_init();

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

    /* 6. 电源管理（每 200ms 检查一次） */
    static uint32_t s_power_check_time = 0;
    uint32_t now = board_time_ms();
    if (now - s_power_check_time >= 200) {
        s_power_check_time = now;
        board_power_mgmt_tick();
    }

    /* 7. 非关键路径 (200ms) */
    static uint32_t s_slow_tick_time = 0;
    if (now - s_slow_tick_time >= 200) {
        s_slow_tick_time = now;
        ble_manager_poll();
        update_ble_state_logging();
    }
}

/* ===================== 应用清理 ===================== */
void app_cleanup(void)
{
    ble_manager_stop_advertising();
    board_vibrate_off();
    board_leds_off();
}

/* 在系统就绪后启动应用级服务 */
esp_err_t app_start_services(void)
{
    ble_state_t state = ble_manager_get_state();
    ESP_LOGI(APP_TAG, "BLE state before advertising: %d", state);

    if (state == BLE_STATE_UNINITIALIZED || state == BLE_STATE_ERROR) {
        ESP_LOGE(APP_TAG, "BLE 未就绪 state=%d", state);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    for (int i = 0; i < BLE_ADV_RETRY_COUNT; i++) {
        ret = ble_manager_start_advertising();
        if (ret == ESP_OK) {
            ESP_LOGI(APP_TAG, "BLE 广播启动成功 (尝试 %d/%d)", i + 1, BLE_ADV_RETRY_COUNT);
            break;
        }
        ESP_LOGW(APP_TAG, "BLE 广播启动失败 (尝试 %d/%d): %s",
                 i + 1, BLE_ADV_RETRY_COUNT, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(BLE_ADV_RETRY_DELAY_MS));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(APP_TAG, "BLE 广播启动失败，系统无法正常运行");
    }

    return ret;
}
