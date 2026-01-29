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

/* 时间同步数据结构体 (对应自定义协议) */
typedef struct {
    uint8_t hour;        // 小时 (0-23)
    uint8_t minute;      // 分钟 (0-59)
    uint8_t second;      // 秒钟 (0-59)
    uint8_t weekday;     // 星期 (0-6, 0=周一)
} ble_time_sync_t;

/* CTS (Current Time Service) 数据结构体 - 蓝牙标准服务 */
typedef struct {
    uint16_t year;       // 年份
    uint8_t month;       // 月份 (1-12)
    uint8_t day;         // 日期 (1-31)
    uint8_t hour;        // 小时 (0-23)
    uint8_t minute;      // 分钟 (0-59)
    uint8_t second;      // 秒钟 (0-59)
    uint8_t weekday;     // 星期 (0-6, 1=周一, 0=周日)
    uint8_t fractions;   // 分数秒 (1/256秒单位)
    uint8_t adjust_reason; // 调整原因 (bit 0-7表示不同原因)
} ble_cts_time_t;

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
 * @brief 解析 CTS (Current Time Service) 数据包
 * CTS 格式：2字节年份(小端) + 月 + 日 + 时 + 分 + 秒 + 星期 + 分数秒 + 调整原因
 * 总长度：10 字节
 *
 * @param data 原始数据指针
 * @param len 数据长度
 * @param out_time CTS时间结果输出
 * @return true 解析成功
 * @return false 解析失败（数据长度不足等）
 */
bool ble_protocol_parse_cts_time(const uint8_t *data, uint16_t len, ble_cts_time_t *out_time);

/**
 * @brief 创建 CTS Current Time 响应数据 (设备发送当前时间)
 *
 * @param time_info CTS 时间信息
 * @param out_data 输出数据缓冲区
 * @param out_len 输出数据长度
 * @return true 创建成功
 * @return false 创建失败
 */
bool ble_protocol_create_cts_response(const ble_cts_time_t *time_info, uint8_t *out_data, uint16_t *out_len);
