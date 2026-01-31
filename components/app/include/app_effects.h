#pragma once
#include "ble_manager.h"
#include <stdbool.h>

void app_effects_apply(const ble_effect_t* effect);
void app_effects_tick(void);
bool app_effects_is_active(void);
