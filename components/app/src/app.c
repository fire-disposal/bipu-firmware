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

static const char* APP_TAG = "app";
/* ========== 配置常量（替代魔法数字） ========== */
#define BATTERY_UPDATE_INTERVAL_MS 60000U
#define CONNECT_BLINK_DURATION_MS 3000U
#define CONNECT_BLINK_INTERVAL_MS 200U

/* ===================== BLE 消息接收回调 ===================== */
/* ===================== CTS 时间同步回调 ===================== */
/* BLE 回调实现已移到 app_ble.c */
#include "app_ble.h"

/* connection state machine is implemented in app_conn_sm.c */

/* message effects are implemented in app_effects.c */

/* battery management is implemented in app_battery.c */

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    ESP_LOGI(APP_TAG, "初始化应用层...");
    
    esp_err_t ret = ESP_OK;
    
    // 初始化 BLE（包括控制器、栈和广告）
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(APP_TAG, "BLE 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGD(APP_TAG, "BLE 初始化成功");
    
    // 设置 BLE 消息接收回调
    ble_manager_set_message_callback(ble_message_received);

    // 设置 CTS 时间同步回调 (蓝牙标准 Current Time Service)
    ble_manager_set_cts_time_callback(ble_cts_time_received);

    // 启动 BLE 广告
    ret = ble_manager_start_advertising();
    if (ret != ESP_OK) {
        ESP_LOGE(APP_TAG, "BLE 广告启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGD(APP_TAG, "BLE 广告已启动");
    
    // UI 初始化
    ui_init();
    
    ESP_LOGI(APP_TAG, "应用层初始化完成");
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

    // UI 与硬件 tick
    ui_tick();
    board_vibrate_tick();

    // BLE 轮询（处理 BLE 事件）
    ble_manager_poll();

    // 各模块的定期/短时处理
    app_effects_tick();
    // effects 优先：如果 effect 在播放，可选择跳过 conn tick，但 conn tick 内也不再干扰灯光
    app_conn_sm_tick(ble_manager_is_connected());
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
    
    // 关闭 RGB 灯
    board_rgb_off();
    
    ESP_LOGI(APP_TAG, "应用层清理完成");
}
