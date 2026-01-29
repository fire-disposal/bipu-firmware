#include "ble_protocol.h"
#include "ble_config.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_protocol";

static uint8_t calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

bool ble_protocol_parse(const uint8_t *data, uint16_t len, ble_parsed_msg_t *out_msg) {
    if (!data || !out_msg || len < 9) {
        return false;
    }

    // 1. Checksum Validation
    uint8_t received_checksum = data[len - 1];
    uint8_t calculated_checksum = calculate_checksum(data, len - 1);
    if (received_checksum != calculated_checksum) {
        ESP_LOGE(TAG, "Checksum failed: Recv 0x%02X, Calc 0x%02X", received_checksum, calculated_checksum);
        return false;
    }

    // 2. Protocol Version
    if (data[0] != PROTOCOL_VERSION) {
        ESP_LOGE(TAG, "Invalid Protocol Version: 0x%02X", data[0]);
        return false;
    }

    // 3. Command Type
    if (data[1] != CMD_TYPE_MESSAGE) {
        ESP_LOGW(TAG, "Unsupported Command Type: 0x%02X", data[1]);
        return false;
    }

    memset(out_msg, 0, sizeof(ble_parsed_msg_t));
    strcpy(out_msg->sender, "App"); // Default sender

    // Offset tracking
    size_t offset = 4; // Skip Ver, Type, Seq(2)

    // 4. Colors
    uint8_t color_count = data[offset++];
    if (offset + color_count * 3 > len) return false;

    if (color_count > 0) {
        out_msg->effect.r = data[offset];
        out_msg->effect.g = data[offset + 1];
        out_msg->effect.b = data[offset + 2];
        out_msg->effect.duration_ms = 3000;
    }
    offset += color_count * 3;

    // 5. Vibration
    if (offset + 2 > len) return false;
    out_msg->vib_mode = data[offset++];
    uint8_t vib_strength = data[offset++]; // Ignored for now
    (void)vib_strength;

    // 6. Text
    if (offset + 1 > len) return false;
    uint8_t text_len = data[offset++];
    if (offset + text_len > len) return false;

    if (text_len > 64) text_len = 64;
    memcpy(out_msg->message, &data[offset], text_len);
    out_msg->message[text_len] = '\0';
    offset += text_len;

    // 7. Screen Effect
    if (offset + 1 > len) return false;
    out_msg->screen_effect = data[offset++];

    return true;
}

/* ================== CTS (Current Time Service) 协议实现 ================== */

bool ble_protocol_parse_cts_time(const uint8_t *data, uint16_t len, ble_cts_time_t *out_time) {
    if (!data || !out_time || len < 10) {
        ESP_LOGE(TAG, "CTS 数据长度不足: %d (需要至少10字节)", len);
        return false;
    }

    // CTS Current Time 格式 (10字节):
    // Offset 0-1: 年份 (little-endian, uint16)
    // Offset 2:   月份 (1-12)
    // Offset 3:   日期 (1-31)
    // Offset 4:   小时 (0-23)
    // Offset 5:   分钟 (0-59)
    // Offset 6:   秒钟 (0-59)
    // Offset 7:   星期 (0=周日, 1=周一, ..., 6=周六)
    // Offset 8:   分数秒 (1/256秒单位, 0-255)
    // Offset 9:   调整原因 (可选，用于解释时间更新的原因)

    // 解析年份 (小端字节序)
    out_time->year = (uint16_t)(data[0] | (data[1] << 8));

    // 解析月份
    out_time->month = data[2];

    // 解析日期
    out_time->day = data[3];

    // 解析时间
    out_time->hour = data[4];
    out_time->minute = data[5];
    out_time->second = data[6];

    // 解析星期
    out_time->weekday = data[7];

    // 解析分数秒
    out_time->fractions = data[8];

    // 解析调整原因
    out_time->adjust_reason = data[9];

    // 验证时间数据的有效性
    if (out_time->month < 1 || out_time->month > 12 ||
        out_time->day < 1 || out_time->day > 31 ||
        out_time->hour > 23 || out_time->minute > 59 || out_time->second > 59 ||
        out_time->weekday > 6) {
        ESP_LOGE(TAG, "CTS 时间数据无效: %04d-%02d-%02d %02d:%02d:%02d (weekday=%d)",
                 out_time->year, out_time->month, out_time->day,
                 out_time->hour, out_time->minute, out_time->second, out_time->weekday);
        return false;
    }

    ESP_LOGI(TAG, "CTS 时间解析成功: %04d-%02d-%02d %02d:%02d:%02d (weekday=%d, fractions=%d, reason=0x%02X)",
             out_time->year, out_time->month, out_time->day,
             out_time->hour, out_time->minute, out_time->second,
             out_time->weekday, out_time->fractions, out_time->adjust_reason);

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