#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";

// BLE消息接收回调函数
static void ble_message_received(const char* sender, const char* message)
{
    ESP_LOGI(APP_TAG, "收到BLE消息 - 发送者: %s, 内容: %s", sender, message);
    
    // 调用UI层显示消息
    ui_show_message(sender, message);
}

void app_init(void) {
    ESP_LOGI(APP_TAG, "初始化应用...");
    
    // 初始化震动马达
    board_vibrate_init();
    
    // 初始化RGB灯
    board_rgb_init();
    
    // 初始化BLE
    board_ble_init();
    
    // 设置BLE消息接收回调
    board_ble_set_message_callback(ble_message_received);
    
    // UI初始化
    ui_init();
    
    ESP_LOGI(APP_TAG, "应用初始化完成");
}

void app_loop(void) {
    // 轮询按键事件
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        board_notify(); // 按键反馈
        ui_on_key(key);
    }
    
    // UI主循环
    ui_tick();
    
    // 处理震动状态
    board_vibrate_tick();
    
    // BLE轮询
    board_ble_poll();
}