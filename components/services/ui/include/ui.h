#pragma once

#include "ui_state.h"

// UI 对 app 暴露的唯一接口
void ui_init(void);
void ui_show_message(const char* sender, const char* text);
void ui_tick(void);
void ui_on_key(int key);