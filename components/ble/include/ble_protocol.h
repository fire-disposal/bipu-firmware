/**
 * @file ble_protocol.h
 * @brief BLE 协议解析接口
 *
 * 支持两种消息格式：
 * 1. 简单 UTF-8 文本："From Sender: Message"
 * 2. 二进制协议 (兼容旧版本，仅解析文本内容)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 解析结果结构 ================== */
typedef struct {
    char sender[32];        // 发送者名称
    char message[256];      // 消息内容
} ble_parsed_msg_t;

/* ================== CTS 时间结构 (蓝牙标准 Current Time Service) ================== */
typedef struct {
    uint16_t year;          // 年份 (2000-2099)
    uint8_t month;          // 月份 (1-12)
    uint8_t day;            // 日期 (1-31)
    uint8_t hour;           // 小时 (0-23)
    uint8_t minute;         // 分钟 (0-59)
    uint8_t second;         // 秒钟 (0-59)
    uint8_t weekday;        // 星期 (1=周一，7=周日，0=未知)
    uint8_t fractions;      // 分数秒 (1/256 秒，0-255)
    uint8_t adjust_reason;  // 调整原因
} ble_cts_time_t;

/* ================== 时间同步结构 (简化版本) ================== */
typedef struct {
    uint8_t hour;           // 小时 (0-23)
    uint8_t minute;         // 分钟 (0-59)
    uint8_t second;         // 秒钟 (0-59)
    uint8_t weekday;        // 星期 (0-6, 0=周一)
} ble_time_sync_t;

/* ================== 协议解析接口 ================== */

/**
 * @brief 解析 UTF-8 文本消息
 * 支持格式："From Sender: Message" 或纯文本
 *
 * @param data UTF-8 数据
 * @param len 数据长度
 * @param out_msg 解析结果
 * @return true 解析成功
 */
bool ble_protocol_parse_text(const char* data, uint16_t len, ble_parsed_msg_t* out_msg);

/**
 * @brief 解析二进制协议消息 (兼容旧版本)
 *
 * @param data 原始数据
 * @param len 数据长度
 * @param out_msg 解析结果
 * @return true 解析成功
 */
bool ble_protocol_parse(const uint8_t* data, uint16_t len, ble_parsed_msg_t* out_msg);

/**
 * @brief 解析 CTS (Current Time Service) 数据
 * 格式：10 字节 (Exact Time 256)
 *
 * @param data 原始数据
 * @param len 数据长度
 * @param out_time 解析结果
 * @return true 解析成功
 */
bool ble_protocol_parse_cts_time(const uint8_t* data, uint16_t len, ble_cts_time_t* out_time);

/**
 * @brief 创建 CTS 响应数据
 *
 * @param time_info 时间信息
 * @param out_data 输出缓冲区
 * @param out_len 输出长度
 * @return true 创建成功
 */
bool ble_protocol_create_cts_response(const ble_cts_time_t* time_info,
                                       uint8_t* out_data, uint16_t* out_len);

#ifdef __cplusplus
}
#endif
