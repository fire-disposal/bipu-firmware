#include "board.h"
#include "ble_manager.h"
#include "ui.h"
#include "app.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* APP_TAG = "app";

/* ===================== BLE 消息接收回调 ===================== */
static uint32_t s_message_effect_end_time = 0;

static void ble_message_received(const char* sender, const char* message, const ble_effect_t* effect)
{
    if (!sender || !message) {
        ESP_LOGW(APP_TAG, "BLE 回调接收到无效参数");
        return;
    }
    
    ESP_LOGI(APP_TAG, "BLE 消息已接收 - 发送者: %s, 内容: %s", sender, message);
    
    // 处理光效
    if (effect && effect->duration_ms > 0) {
        ESP_LOGI(APP_TAG, "消息附带光效: R=%d, G=%d, B=%d, 持续=%dms", 
                 effect->r, effect->g, effect->b, effect->duration_ms);

        // 直接使用 RGB 值，不再进行颜色映射
        board_rgb_t color = { .r = effect->r, .g = effect->g, .b = effect->b };
        
        // 只有当颜色不全为0时才设置
        if (color.r != 0 || color.g != 0 || color.b != 0) {
            board_rgb_set(color);
            s_message_effect_end_time = board_time_ms() + effect->duration_ms;
        }
    }
    
    // 调用 UI 层显示消息
    ui_show_message(sender, message);
}

/* ===================== 应用初始化 ===================== */
esp_err_t app_init(void)
{
    ESP_LOGI(APP_TAG, "初始化应用层...");
    
    esp_err_t ret = ESP_OK;
    
    // 震动马达和RGB灯已在 board_init() 中初始化，此处不再重复初始化
    
    // 初始化 BLE（包括控制器、栈和广告）
    ret = ble_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(APP_TAG, "BLE 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGD(APP_TAG, "BLE 初始化成功");
    
    // 设置 BLE 消息接收回调
    ble_manager_set_message_callback(ble_message_received);
    
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
        // 按键反馈（轻微颤动）
        board_vibrate_on(20);
        
        // 处理按键
        ui_on_key(key);
    }
    
    // UI 主循环（刷新屏幕、更新状态）
    ui_tick();
    
    // 处理振动马达状态（定时关闭）
    board_vibrate_tick();
    
    // BLE 轮询（处理 BLE 事件）
    ble_manager_poll();

    // 模拟电池电量更新 (每60秒)
    static uint32_t last_battery_update = 0;
    if (board_time_ms() - last_battery_update > 60000) {
        last_battery_update = board_time_ms();
        // TODO: 读取实际电池电压
        ble_manager_update_battery_level(85); 
    }

    // BLE 连接状态指示（蓝灯闪烁）
    static bool last_connected_state = false;
    static uint32_t connection_start_time = 0;
    bool current_connected_state = ble_manager_is_connected();
    
    // 如果正在播放消息光效，跳过连接状态指示
    if (board_time_ms() < s_message_effect_end_time) {
        // Do nothing, let the effect play
    } else {
        // 消息光效结束，恢复正常状态
        
        if (current_connected_state) {
            if (!last_connected_state) {
                // 刚连接上，记录时间
                connection_start_time = board_time_ms();
            }
            
            // 连接成功后只闪烁 3 秒
            if (board_time_ms() - connection_start_time < 3000) {
                static uint32_t last_blink_time = 0;
                uint32_t current_time = board_time_ms();
                
                if (current_time - last_blink_time > 200) { // 加快闪烁频率 (5Hz)
                    static bool led_state = false;
                    if (led_state) {
                        board_rgb_set(BOARD_COLOR_BLUE);
                    } else {
                        board_rgb_off();
                    }
                    led_state = !led_state;
                    last_blink_time = current_time;
                }
            } else {
                // 连接稳定后，关闭灯光
                board_rgb_off();
            }
        } else if (last_connected_state) {
            // 刚断开连接，关闭 RGB 灯
            board_rgb_off();
        }
    }
    last_connected_state = current_connected_state;
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
