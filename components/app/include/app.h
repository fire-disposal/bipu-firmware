#pragma once
#include "esp_err.h"

// app 生命周期接口
esp_err_t app_init(void);
void app_loop(void);
void app_cleanup(void);
