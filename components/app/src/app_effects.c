#include "app_effects.h"
#include "board.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_effects";
static uint32_t s_message_effect_end_time = 0;

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

void app_effects_tick(void)
{
    if (s_message_effect_end_time == 0) return;
    uint32_t now = board_time_ms();
    if (now >= s_message_effect_end_time) {
        s_message_effect_end_time = 0;
        board_leds_off();
    }
}

bool app_effects_is_active(void)
{
    if (s_message_effect_end_time == 0) return false;
    return board_time_ms() < s_message_effect_end_time;
}
