#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief 初始化系统定时器
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t timer_init(void);

/**
 * @brief 获取系统运行时间（毫秒）
 * @return 运行时间（毫秒）
 */
uint32_t timer_get_ms(void);

/**
 * @brief 延时毫秒
 * @param ms 延时时间（毫秒）
 */
void timer_delay_ms(uint32_t ms);

/**
 * @brief 获取系统运行时间（微秒）
 * @return 运行时间（微秒）
 */
uint64_t timer_get_us(void);

/**
 * @brief 延时微秒
 * @param us 延时时间（微秒）
 */
void timer_delay_us(uint32_t us);