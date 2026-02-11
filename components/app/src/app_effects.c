#include "app_effects.h"
#include "board.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_effects";
static uint32_t s_message_effect_end_time = 0;
static uint32_t s_notify_blink_end_time = 0;
static uint32_t s_last_blink_toggle = 0;
static bool s_blink_state = false;

#define BLINK_INTERVAL_MS 200

void app_effects_apply(const ble_effect_t* effect)
{
    if (!effect) return;
    if (effect->duration_ms == 0) return;

    esp_log_level_set(TAG, ESP_LOG_INFO);
    // 保留蓝牙协议消息结构，但不执行实际 LED 播放（硬件已改为白光灯）
    ESP_LOGI(TAG, "Received message effect r=%d g=%d b=%d duration=%dms (playback suppressed)",
             effect->r, effect->g, effect->b, effect->duration_ms);
    if (effect->r == 0 && effect->g == 0 && effect->b == 0) return;

    // 仅记录播放时长以便上层查询/避让，但不实际点亮
    s_message_effect_end_time = board_time_ms() + (uint32_t)effect->duration_ms;
}

void app_effects_notify_blink(uint32_t duration_ms)
{
    if (duration_ms == 0) duration_ms = 3000; // 默认3秒
    
    s_notify_blink_end_time = board_time_ms() + duration_ms;
    s_last_blink_toggle = board_time_ms();
    s_blink_state = true;
    
    // 立即点亮
    board_leds_t leds = { .led1 = 255, .led2 = 255, .led3 = 255 };
    board_leds_set(leds);
    
    ESP_LOGI(TAG, "Notify blink started for %lu ms", duration_ms);
}

void app_effects_tick(void)
{
    uint32_t now = board_time_ms();
    
    // 处理来信闪烁效果
    if (s_notify_blink_end_time != 0) {
        if (now >= s_notify_blink_end_time) {
            // 闪烁结束
            s_notify_blink_end_time = 0;
            board_leds_off();
            ESP_LOGD(TAG, "Notify blink ended");
        } else if (now - s_last_blink_toggle >= BLINK_INTERVAL_MS) {
            // 切换闪烁状态
            s_blink_state = !s_blink_state;
            s_last_blink_toggle = now;
            
            if (s_blink_state) {
                board_leds_t leds = { .led1 = 255, .led2 = 255, .led3 = 255 };
                board_leds_set(leds);
            } else {
                board_leds_off();
            }
        }
        return; // 闪烁优先
    }
    
    // 处理消息效果
    if (s_message_effect_end_time == 0) return;
    if (now >= s_message_effect_end_time) {
        s_message_effect_end_time = 0;
        board_leds_off();
    }
}

bool app_effects_is_active(void)
{
    uint32_t now = board_time_ms();
    
    if (s_notify_blink_end_time != 0 && now < s_notify_blink_end_time) {
        return true;
    }
    
    if (s_message_effect_end_time == 0) return false;
    return now < s_message_effect_end_time;
}
