#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化电池保护模块
 * 设置硬件层低电压保护回调
 */
void app_battery_init(void);

#ifdef __cplusplus
}
#endif
