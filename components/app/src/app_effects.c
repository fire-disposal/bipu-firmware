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
    board_rgb_t color = { .r = effect->r, .g = effect->g, .b = effect->b };
    if (color.r == 0 && color.g == 0 && color.b == 0) return;

    board_rgb_set(color);
    s_message_effect_end_time = board_time_ms() + (uint32_t)effect->duration_ms;
}

void app_effects_tick(void)
{
    if (s_message_effect_end_time == 0) return;
    uint32_t now = board_time_ms();
    if (now >= s_message_effect_end_time) {
        s_message_effect_end_time = 0;
        board_rgb_off();
    }
}

bool app_effects_is_active(void)
{
    if (s_message_effect_end_time == 0) return false;
    return board_time_ms() < s_message_effect_end_time;
}
