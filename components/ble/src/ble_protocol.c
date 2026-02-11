/**
 * @file ble_protocol.c
 * @brief BLE 协议解析实现
 * 
 * 支持两种消息格式：
 * 1. 简单 UTF-8 文本: "From Sender: Message" 或纯文本
 * 2. 带效果的二进制协议 (兼容旧版本)
 */

#include "ble_protocol.h"
#include "ble_config.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char* TAG = "ble_protocol";

/* ================== 辅助函数 ================== */

/**
 * @brief 计算校验和 (用于二进制协议)
 */
static uint8_t calculate_checksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief 跳过前导空白字符
 */
static const char* skip_whitespace(const char* str)
{
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/* ================== 文本协议解析 ================== */

bool ble_protocol_parse_text(const char* data, uint16_t len, ble_parsed_msg_t* out_msg)
{
    if (!data || !out_msg || len == 0) {
        return false;
    }

    memset(out_msg, 0, sizeof(ble_parsed_msg_t));
    
    // 默认发送者
    strcpy(out_msg->sender, "App");
    
    // 默认效果 (来信闪烁)
    out_msg->effect.r = 255;
    out_msg->effect.g = 255;
    out_msg->effect.b = 255;
    out_msg->effect.duration_ms = 3000;
    out_msg->vib_mode = 1;

    // 尝试解析 "From Sender: Message" 格式
    const char* from_prefix = "From ";
    if (len > 6 && strncasecmp(data, from_prefix, 5) == 0) {
        const char* colon = strchr(data + 5, ':');
        if (colon) {
            // 提取发送者名称
            size_t sender_len = colon - (data + 5);
            if (sender_len > 0 && sender_len < sizeof(out_msg->sender)) {
                strncpy(out_msg->sender, data + 5, sender_len);
                out_msg->sender[sender_len] = '\0';
                
                // 去除发送者名称末尾空格
                while (sender_len > 0 && out_msg->sender[sender_len - 1] == ' ') {
                    out_msg->sender[--sender_len] = '\0';
                }
            }

            // 提取消息内容
            const char* msg_start = skip_whitespace(colon + 1);
            size_t msg_len = len - (msg_start - data);
            if (msg_len > 0 && msg_len < sizeof(out_msg->message)) {
                strncpy(out_msg->message, msg_start, msg_len);
                out_msg->message[msg_len] = '\0';
            }

            ESP_LOGI(TAG, "Parsed text message - Sender: %s, Message: %s", 
                     out_msg->sender, out_msg->message);
            return true;
        }
    }

    // 纯文本消息
    size_t copy_len = len;
    if (copy_len >= sizeof(out_msg->message)) {
        copy_len = sizeof(out_msg->message) - 1;
    }
    strncpy(out_msg->message, data, copy_len);
    out_msg->message[copy_len] = '\0';

    ESP_LOGI(TAG, "Parsed plain text message: %s", out_msg->message);
    return true;
}

/* ================== 二进制协议解析 (兼容旧版本) ================== */

bool ble_protocol_parse(const uint8_t* data, uint16_t len, ble_parsed_msg_t* out_msg)
{
    if (!data || !out_msg) {
        return false;
    }

    // 检查是否是 UTF-8 文本 (简单启发式判断)
    // 如果第一个字节是可打印 ASCII 或 UTF-8 多字节序列开头，认为是文本
    if (len > 0) {
        uint8_t first_byte = data[0];
        bool is_text = (first_byte >= 0x20 && first_byte < 0x7F) || // 可打印 ASCII
                       (first_byte >= 0xC0);                         // UTF-8 多字节开头
        
        if (is_text) {
            return ble_protocol_parse_text((const char*)data, len, out_msg);
        }
    }

    // 二进制协议解析 (需要最少 9 字节)
    if (len < 9) {
        ESP_LOGW(TAG, "Data too short for binary protocol: %d bytes", len);
        return ble_protocol_parse_text((const char*)data, len, out_msg);
    }

    // 校验和验证
    uint8_t received_checksum = data[len - 1];
    uint8_t calculated_checksum = calculate_checksum(data, len - 1);
    if (received_checksum != calculated_checksum) {
        ESP_LOGW(TAG, "Checksum mismatch, treating as text");
        return ble_protocol_parse_text((const char*)data, len, out_msg);
    }

    // 协议版本检查
    if (data[0] != 0x01) {
        ESP_LOGW(TAG, "Unknown protocol version: 0x%02X", data[0]);
        return ble_protocol_parse_text((const char*)data, len, out_msg);
    }

    memset(out_msg, 0, sizeof(ble_parsed_msg_t));
    strcpy(out_msg->sender, "App");

    size_t offset = 4; // 跳过 Ver, Type, Seq(2)

    // 颜色数据
    if (offset >= len) return false;
    uint8_t color_count = data[offset++];
    if (offset + color_count * 3 > len) return false;

    if (color_count > 0) {
        out_msg->effect.r = data[offset];
        out_msg->effect.g = data[offset + 1];
        out_msg->effect.b = data[offset + 2];
        out_msg->effect.duration_ms = 3000;
    }
    offset += color_count * 3;

    // 震动数据
    if (offset + 2 > len) return false;
    out_msg->vib_mode = data[offset++];
    offset++; // 跳过震动强度

    // 文本数据
    if (offset + 1 > len) return false;
    uint8_t text_len = data[offset++];
    if (offset + text_len > len) return false;

    // 限制文本长度不超过缓冲区
    size_t max_text_len = sizeof(out_msg->message) - 1;
    size_t copy_len = (text_len > max_text_len) ? max_text_len : text_len;
    memcpy(out_msg->message, &data[offset], copy_len);
    out_msg->message[copy_len] = '\0';
    out_msg->message[text_len] = '\0';
    offset += text_len;

    // 屏幕效果
    if (offset + 1 <= len) {
        out_msg->screen_effect = data[offset++];
    }

    ESP_LOGI(TAG, "Parsed binary message: %s", out_msg->message);
    return true;
}

/* ================== CTS (Current Time Service) 协议实现 ================== */

bool ble_protocol_parse_cts_time(const uint8_t* data, uint16_t len, ble_cts_time_t* out_time)
{
    if (!data || !out_time || len < 10) {
        ESP_LOGE(TAG, "CTS data too short: %d bytes (need 10)", len);
        return false;
    }

    /*
     * CTS Exact Time 256 格式 (10 字节):
     * Offset 0-1: 年份 (uint16, Little Endian, 2000-2099)
     * Offset 2:   月份 (uint8, 1-12)
     * Offset 3:   日期 (uint8, 1-31)
     * Offset 4:   小时 (uint8, 0-23)
     * Offset 5:   分钟 (uint8, 0-59)
     * Offset 6:   秒钟 (uint8, 0-59)
     * Offset 7:   星期 (uint8, 1=周一...7=周日, 0=未知)
     * Offset 8:   毫秒高位 (uint8, 毫秒 * 256 / 1000)
     * Offset 9:   调整原因 (uint8, 通常为 0)
     */

    // 年份 (Little Endian)
    out_time->year = (uint16_t)(data[0] | (data[1] << 8));
    out_time->month = data[2];
    out_time->day = data[3];
    out_time->hour = data[4];
    out_time->minute = data[5];
    out_time->second = data[6];
    out_time->weekday = data[7];
    out_time->fractions = data[8];
    out_time->adjust_reason = data[9];

    // 验证数据有效性
    bool valid = true;
    
    if (out_time->year < 2000 || out_time->year > 2099) {
        ESP_LOGW(TAG, "CTS year out of range: %d", out_time->year);
        valid = false;
    }
    if (out_time->month < 1 || out_time->month > 12) {
        ESP_LOGW(TAG, "CTS month invalid: %d", out_time->month);
        valid = false;
    }
    if (out_time->day < 1 || out_time->day > 31) {
        ESP_LOGW(TAG, "CTS day invalid: %d", out_time->day);
        valid = false;
    }
    if (out_time->hour > 23) {
        ESP_LOGW(TAG, "CTS hour invalid: %d", out_time->hour);
        valid = false;
    }
    if (out_time->minute > 59) {
        ESP_LOGW(TAG, "CTS minute invalid: %d", out_time->minute);
        valid = false;
    }
    if (out_time->second > 59) {
        ESP_LOGW(TAG, "CTS second invalid: %d", out_time->second);
        valid = false;
    }
    if (out_time->weekday > 7) {
        ESP_LOGW(TAG, "CTS weekday invalid: %d", out_time->weekday);
        valid = false;
    }

    if (!valid) {
        ESP_LOGE(TAG, "CTS time validation failed");
        return false;
    }

    ESP_LOGI(TAG, "CTS time parsed: %04d-%02d-%02d %02d:%02d:%02d (weekday=%d)",
             out_time->year, out_time->month, out_time->day,
             out_time->hour, out_time->minute, out_time->second, out_time->weekday);

    return true;
}

bool ble_protocol_create_cts_response(const ble_cts_time_t *time_info, uint8_t *out_data, uint16_t *out_len) {
    if (!time_info || !out_data || !out_len) {
        return false;
    }

    if (*out_len < 10) {
        ESP_LOGE(TAG, "CTS 响应缓冲区不足: %d (需要10字节)", *out_len);
        return false;
    }

    // 构建 CTS Current Time 格式 (10字节)
    // Offset 0-1: 年份 (小端字节序)
    out_data[0] = (uint8_t)(time_info->year & 0xFF);
    out_data[1] = (uint8_t)((time_info->year >> 8) & 0xFF);

    // Offset 2: 月份
    out_data[2] = time_info->month;

    // Offset 3: 日期
    out_data[3] = time_info->day;

    // Offset 4-6: 时间
    out_data[4] = time_info->hour;
    out_data[5] = time_info->minute;
    out_data[6] = time_info->second;

    // Offset 7: 星期
    out_data[7] = time_info->weekday;

    // Offset 8: 分数秒
    out_data[8] = time_info->fractions;

    // Offset 9: 调整原因
    out_data[9] = time_info->adjust_reason;

    *out_len = 10;

    ESP_LOGI(TAG, "CTS 响应已创建: %04d-%02d-%02d %02d:%02d:%02d",
             time_info->year, time_info->month, time_info->day,
             time_info->hour, time_info->minute, time_info->second);

    return true;
}