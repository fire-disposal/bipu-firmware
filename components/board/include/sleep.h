#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sleep.h
 * @brief 电源管理模块（Light Sleep 实现）
 * 
 * 电源状态机：
 *   ACTIVE (活跃) ──60s 无操作──> BLACK_SCREEN (黑屏)
 *                                  │
 *                                  │ 240s 无操作
 *                                  ↓
 *                           LIGHT_SLEEP (浅睡眠)
 *                                  │
 *                      ┌───────────┴───────────┐
 *                      │ 按键/BLE 消息唤醒       │
 *                      └───────────────────────┘
 * 
 * 功耗对比：
 *   - ACTIVE:      ~80mA（CPU + 显示 + BLE）
 *   - BLACK_SCREEN: ~15mA（CPU + BLE，关闭显示）
 *   - LIGHT_SLEEP:  ~1-2mA（仅 BLE 保持，CPU 睡眠）
 */

/* ================== 电源状态定义 ================== */

typedef enum {
    POWER_STATE_ACTIVE,       /** 活跃状态：CPU + 显示 + BLE 全速运行 */
    POWER_STATE_BLACK_SCREEN, /** 黑屏状态：关闭显示，BLE 保持连接 */
    POWER_STATE_LIGHT_SLEEP,  /** 浅睡眠状态：CPU 睡眠，BLE 保持连接 */
} power_state_t;

/* ================== 配置常量 ================== */

/** 进入黑屏的超时时间（毫秒）- 60 秒无操作 */
#define POWER_BLACK_SCREEN_TIMEOUT_MS   (60000U)

/** 进入 Light Sleep 的超时时间（毫秒）- 黑屏后额外 240 秒（总计 300 秒） */
#define POWER_LIGHT_SLEEP_TIMEOUT_MS    (300000U)

/* ================== 核心接口 ================== */

/**
 * @brief 电源管理模块初始化
 * 
 * 在 board_init() 中调用，配置唤醒源和电源管理参数
 * 
 * @return esp_err_t 
 *   - ESP_OK: 初始化成功
 *   - ESP_FAIL: 初始化失败
 */
esp_err_t board_power_mgmt_init(void);

/**
 * @brief 获取当前电源状态
 * 
 * @return power_state_t 当前电源状态
 */
power_state_t board_power_mgmt_get_state(void);

/**
 * @brief 获取最后活动时间戳
 * 
 * @return uint32_t 最后活动时间（board_time_ms 返回值）
 */
uint32_t board_power_mgmt_get_last_activity_time(void);

/**
 * @brief 重置活动计时器
 * 
 * 在用户交互（按键、触摸等）时调用，重置超时计时器
 */
void board_power_mgmt_reset_activity_timer(void);

/**
 * @brief 电源管理_tick（在 app_loop 中调用）
 * 
 * 检查超时并自动切换电源状态
 * 必须在 app_task 上下文中调用（非锁内）
 */
void board_power_mgmt_tick(void);

/**
 * @brief 进入指定电源状态
 * 
 * @param state 目标电源状态
 * @return esp_err_t 
 *   - ESP_OK: 成功
 *   - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t board_power_mgmt_enter_state(power_state_t state);

/**
 * @brief 从睡眠中唤醒
 * 
 * 唤醒后调用，恢复硬件状态
 */
void board_power_mgmt_wake_up(void);

/**
 * @brief 检查是否从睡眠唤醒
 * 
 * 在 main.c 启动时调用，判断启动原因
 * 
 * @return true 从睡眠唤醒
 * @return false 冷启动
 */
bool board_power_mgmt_is_wakeup_from_sleep(void);

/**
 * @brief 获取唤醒原因
 * 
 * @return esp_sleep_source_t 唤醒源
 */
esp_sleep_source_t board_power_mgmt_get_wakeup_cause(void);

/**
 * @brief 检查是否从 BLE 事件唤醒
 * 
 * @return true BLE 事件唤醒
 * @return false 其他唤醒源
 */
bool board_power_mgmt_is_wakeup_from_ble(void);

/**
 * @brief 检查是否从按键唤醒
 * 
 * @return true 按键唤醒
 * @return false 其他唤醒源
 */
bool board_power_mgmt_is_wakeup_from_gpio(void);

/* ================== 内部接口（勿在应用层调用） ================== */

/**
 * @brief 配置 GPIO 唤醒源
 * 
 * 配置 4 个按键为唤醒源（仅在进入 Light Sleep 前调用）
 */
void board_power_mgmt_configure_gpio_wakeup(void);

/**
 * @brief 进入 Light Sleep（底层实现）
 * 
 * 此函数不会返回，唤醒后从 app_main 继续执行
 */
void board_power_mgmt_enter_light_sleep(void);

#ifdef __cplusplus
}
#endif
