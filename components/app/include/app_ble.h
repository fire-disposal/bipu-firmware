#pragma once
#include "ble_manager.h"

void ble_message_received(const char* sender, const char* message, const ble_effect_t* effect);
void ble_cts_time_received(const ble_cts_time_t* cts_time);
