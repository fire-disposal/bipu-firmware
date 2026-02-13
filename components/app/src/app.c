#include "app.h"
#include "app_effects.h"
#include "app_battery.h"
#include "app_conn_sm.h"
#include "app_ble.h"
#include "board.h"
#include "board_hal.h"
#include "ble_manager.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_err.h"
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

/* LED 状态指示 */
typedef enum {
    LED_STATE_OFF,
    LED_STATE_ADV_MARQUEE,   // 广播跑马灯
    LED_STATE_CONNECT_BLINK, // 连接闪烁
} led_state_t;

static led_state_t s_led_state = LED_STATE_OFF;
static uint32_t s_led_last_change = 0;
static int s_marquee_index = 0; // 当前点亮的 LED 索引 (0-2)
static int s_blink_count = 0;   // 已闪烁次数

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

        // 设置回调
        ble_manager_set_message_callback(ble_message_received);
        ble_manager_set_cts_time_callback(ble_cts_time_received);

        // BLE 广播延后启动：由系统入口在所有初始化完成后触发
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

static void app_led_tick(void) {
    uint32_t now = board_time_ms();
    // 优先级处理：手电筒与效果/通知应优先，不应被常规 LED 状态覆盖；待机时保持熄灭
    if (ui_is_flashlight_on()) {
        return; // UI 已直接控制 LED（手电筒），保持当前状态
    }
    if (app_effects_is_active()) {
        return; // 特效（如通知闪烁）优先，占用 LED
    }
    if (ui_is_in_standby()) {
        board_leds_off();
        return; // 休眠优先，保持 LED 熄灭
    }
    
    // 根据 BLE 状态更新 LED 状态
    bool connected = ble_manager_is_connected();
    bool advertising = (ble_manager_get_state() == BLE_STATE_ADVERTISING);
    
    // 状态转换
    if (connected && s_led_state != LED_STATE_CONNECT_BLINK) {
        s_led_state = LED_STATE_CONNECT_BLINK;
        s_blink_count = 0;
        s_led_last_change = now;
        board_leds_off();
    } else if (!connected && advertising && s_led_state != LED_STATE_ADV_MARQUEE) {
        s_led_state = LED_STATE_ADV_MARQUEE;
        s_marquee_index = 0;
        s_led_last_change = now;
        board_leds_off();
    } else if (!connected && !advertising && s_led_state != LED_STATE_OFF) {
        s_led_state = LED_STATE_OFF;
        board_leds_off();
    }
    
    // 状态行为
    switch (s_led_state) {
        case LED_STATE_ADV_MARQUEE:
            // 每 300ms 切换一次 LED
            if (now - s_led_last_change >= 300) {
                s_led_last_change = now;
                board_leds_t leds = {0, 0, 0};
                if (s_marquee_index == 0) leds.led1 = 255;
                else if (s_marquee_index == 1) leds.led2 = 255;
                else if (s_marquee_index == 2) leds.led3 = 255;
                board_leds_set(leds);
                s_marquee_index = (s_marquee_index + 1) % 3;
            }
            break;
        case LED_STATE_CONNECT_BLINK:
            // 闪烁两次，每次亮 200ms，灭 200ms
            if (s_blink_count < 4) { // 总共 4 个半周期（亮-灭-亮-灭）
                if (now - s_led_last_change >= 200) {
                    s_led_last_change = now;
                    board_leds_t leds = {0, 0, 0};
                    if (s_blink_count % 2 == 0) {
                        // 亮
                        leds.led1 = leds.led2 = leds.led3 = 255;
                    } else {
                        // 灭
                        leds.led1 = leds.led2 = leds.led3 = 0;
                    }
                    board_leds_set(leds);
                    s_blink_count++;
                }
            } else {
                // 闪烁完成，熄灭并进入 OFF 状态
                board_leds_off();
                s_led_state = LED_STATE_OFF;
            }
            break;
        case LED_STATE_OFF:
            // 保持熄灭
            break;
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

    // 3. 以下非关键路径每 200ms 执行一次（5Hz）
    static uint32_t s_slow_tick_time = 0;
    uint32_t now = board_time_ms();
    if (now - s_slow_tick_time >= 200) {
        s_slow_tick_time = now;

        // BLE 事件处理（NimBLE 事件驱动，此函数仅做轻量检查）
        ble_manager_poll();

        // 效果处理（LED 闪烁间隔 200ms，5Hz 足够）
        app_effects_tick();
        
        // LED 状态指示（广播跑马灯，连接闪烁）
        app_led_tick();
        
        // 连接状态机（仅在无效果播放时）
        if (!app_effects_is_active()) {
            app_conn_sm_tick(ble_manager_is_connected());
        }
        
        // 电池状态更新（内部已有10s节流）
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
