#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化电池管理模块
 * 启动后台定时器进行电压检测
 */
void app_battery_init(void);

/**
 * @brief 电池状态检查（由定时器自动调用）
 */
void app_battery_tick(void);

#ifdef __cplusplus
}
#endif
