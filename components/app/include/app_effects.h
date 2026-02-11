#pragma once
#include "ble_manager.h"
#include <stdbool.h>

void app_effects_apply(const ble_effect_t* effect);
void app_effects_tick(void);
bool app_effects_is_active(void);

// 来信提醒闪烁效果
void app_effects_notify_blink(uint32_t duration_ms);
