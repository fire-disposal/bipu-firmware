#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 应用初始化接口 ================== */

/**
 * @brief 应用层初始化
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t app_init(void);

/**
 * @brief 应用主循环（需要从主任务定期调用）
 */
void app_loop(void);

/**
 * @brief 应用清理（系统重启前调用）
 */
void app_cleanup(void);

/**
 * @brief 启动应用级服务（BLE广播等）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t app_start_services(void);

#ifdef __cplusplus
}
#endif
