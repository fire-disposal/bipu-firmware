#pragma once
#include "esp_err.h"

// app 生命周期接口
esp_err_t app_init(void);
void app_loop(void);
void app_cleanup(void);
// 在系统就绪（例如开机震动完成）后启动应用级服务（如 BLE 广播）
esp_err_t app_start_services(void);