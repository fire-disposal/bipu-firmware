#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 消息附带的效果（光效、震动等） */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t duration_ms;
} ble_effect_t;

/* 解析结果结构体 */
typedef struct {
    char sender[32];
    char message[65];
    ble_effect_t effect;
    uint8_t vib_mode;
    uint8_t screen_effect;
} ble_parsed_msg_t;

/* 时间同步数据结构体 */
typedef struct {
    uint8_t hour;        // 小时 (0-23)
    uint8_t minute;      // 分钟 (0-59)
    uint8_t second;      // 秒钟 (0-59)
    uint8_t weekday;     // 星期 (0-6, 0=周一)
} ble_time_sync_t;

/**
 * @brief 解析接收到的原始字节包
 *
 * @param data 原始数据指针
 * @param len 数据长度
 * @param out_msg 解析结果输出
 * @return true 解析成功
 * @return false 解析失败（校验和错误、版本不匹配等）
 */
bool ble_protocol_parse(const uint8_t *data, uint16_t len, ble_parsed_msg_t *out_msg);

/**
 * @brief 解析时间同步数据包
 *
 * @param data 原始数据指针
 * @param len 数据长度
 * @param out_time 时间同步结果输出
 * @return true 解析成功
 * @return false 解析失败（校验和错误、格式不匹配等）
 */
bool ble_protocol_parse_time_sync(const uint8_t *data, uint16_t len, ble_time_sync_t *out_time);

/**
 * @brief 创建时间同步响应包
 *
 * @param success 是否成功
 * @param out_data 输出数据缓冲区
 * @param out_len 输出数据长度
 * @return true 创建成功
 * @return false 创建失败
 */
bool ble_protocol_create_time_sync_response(bool success, uint8_t *out_data, uint16_t *out_len);
