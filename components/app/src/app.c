#include "app.h"
#include "app_battery.h"
#include "app_ble.h"
#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";

/* ========== 配置常量 ========== */
// GUI 任务配置（分离 UI 刷屏至低优先级任务）
#define GUI_TASK_PRIORITY    (3)
#define GUI_TASK_PERIOD_MS   (50)
#define GUI_TASK_STACK_SIZE  (4096)

// 任务句柄
static TaskHandle_t s_gui_task_handle = NULL;

/* ===================== GUI 任务 ===================== */
static void gui_task(void* pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(APP_TAG, "GUI 任务已启动 (周期: %d ms)", GUI_TASK_PERIOD_MS);
    
    TickType_t last_wake_time = xTaskGetTickCount();
    
    for (;;) {
        ui_tick();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GUI_TASK_PERIOD_MS));
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
        // 设置消息回调
        ble_manager_set_message_callback(ble_message_received);
        // 注：CTS 时间同步已在 ble_manager 内部处理
    }

    // 2. UI 初始化
    ui_init();

    // 3. 创建 GUI 任务（双核芯片绑 Core 1 避让 BLE，单核芯片绑 Core 0）
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

/* ===================== LED 模式管理 ===================== */
/**
 * 同步应用层状态到 LED 状态机
 * 将 BLE 连接状态、UI 手电筒、待机等映射到 LED 工作模式
 * 200ms 周期调用，CPU 占用极低
 */
static void app_update_led_mode(void)
{
    // 优先级：手电筒 > 待机 > BLE 状态
    
    if (ui_is_flashlight_on()) {
        board_leds_set_mode(BOARD_LED_MODE_STATIC);
    } else if (ui_is_in_standby()) {
        board_leds_set_mode(BOARD_LED_MODE_OFF);
    } else if (ble_manager_is_connected()) {
        board_leds_set_mode(BOARD_LED_MODE_BLINK);
    } else if (ble_manager_get_state() == 1) {  // BLE_STATE_ADVERTISING = 1
        board_leds_set_mode(BOARD_LED_MODE_MARQUEE);
    } else {
        board_leds_set_mode(BOARD_LED_MODE_OFF);
    }
}

/* ===================== 应用主循环 ===================== */
/**
 * 应用主循环 - 高效设计
 *
 * 高频部分（每次调用）：
 *   - 按键轮询：即时响应
 *   - 震动驱动：精确计时
 *   - LED 动画：保证流畅性（board_leds_tick）
 *
 * 中频部分（200ms 周期）：
 *   - LED 模式同步：应用状态 → LED 模式
 *   - BLE 轮询：事件驱动
 *   - 电池监测：功耗优化
 */
void app_loop(void)
{
    // === 高频路径：直接硬件反馈 ===
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        ui_on_key(key);
    }
    board_vibrate_tick();
    
    // 驱动 LED 动画（必须高频以保证平滑）
    board_leds_tick();

    // === 中频路径：200ms 周期 ===
    static uint32_t s_slow_tick_time = 0;
    uint32_t now = board_time_ms();
    
    if (now - s_slow_tick_time >= 200) {
        s_slow_tick_time = now;

        // 同步应用层状态到 LED 状态机（BLE/UI 状态 → LED 模式）
        app_update_led_mode();
        
        // BLE状态已在GAP事件中自动更新，无需轮询
        // 状态变化会通过LED模式更新反映出来
        
        // 电池监测
        app_battery_tick();
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
    
    // 清理 BLE 资源
    ble_manager_cleanup();
    
    // 关闭外设
    board_vibrate_off();
    board_leds_off();
    
    ESP_LOGI(APP_TAG, "应用层清理完成");
}

// 在系统就绪后启动应用级服务（例如 BLE 广播）
esp_err_t app_start_services(void) {
    ESP_LOGI(APP_TAG, "Starting app services (post-init)");
    
    // 简化BLE广播启动，重试逻辑已在ble_manager内部处理
    esp_err_t ret = ble_manager_start_advertising();
    if (ret == ESP_OK) {
        ESP_LOGI(APP_TAG, "BLE 广告已启动 (from app_start_services)");
    } else {
        ESP_LOGW(APP_TAG, "BLE 广告启动失败: %s", esp_err_to_name(ret));
    }
    
    return ret;
}
