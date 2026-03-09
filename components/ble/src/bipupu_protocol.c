/**
 * @file bipupu_protocol.c
 * @brief Bipupu 蓝牙协议解析实现 (版本 1.2)
 * 
 * 协议格式:
 * [协议头 (1 字节)][时间戳 (4 字节)][消息类型 (1 字节)][数据长度 (2 字节)][数据 (N 字节)][校验和 (1 字节)]
 * 
 * 协议头：0xB0 (固定值)
 * 字节序：小端序
 * 校验和：异或校验 (XOR)
 */

#include "bipupu_protocol.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "bipupu_protocol";

/* ================== 内部辅助函数 ================== */

/**
 * @brief 从小端字节序读取 16 位整数
 */
static uint16_t read_le16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/**
 * @brief 从小端字节序读取 32 位整数
 */
static uint32_t read_le32(const uint8_t* data)
{
    return (uint32_t)data[0] | 
           ((uint32_t)data[1] << 8) | 
           ((uint32_t)data[2] << 16) | 
           ((uint32_t)data[3] << 24);
}

/**
 * @brief 写入 16 位整数为小端字节序
 */
static void write_le16(uint8_t* buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
}

/**
 * @brief 写入 32 位整数为小端字节序
 */
static void write_le32(uint8_t* buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[2] = (uint8_t)((value >> 16) & 0xFF);
    buffer[3] = (uint8_t)((value >> 24) & 0xFF);
}

/* ================== 公共接口实现 ================== */

uint8_t bipupu_protocol_calculate_checksum(const uint8_t* data, size_t length)
{
    if (!data || length == 0) {
        return 0;
    }
    
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void bipupu_protocol_decode_utf8_safe(const uint8_t* data, size_t length, char* output)
{
    if (!data || !output || length == 0) {
        if (output) {
            output[0] = '\0';
        }
        return;
    }
    
    size_t i = 0, j = 0;
    const size_t max_output_len = BIPUPU_MAX_DATA_LENGTH;
    
    while (i < length && j < max_output_len) {
        uint8_t first_byte = data[i];
        
        if ((first_byte & 0x80) == 0x00) {
            output[j++] = (char)first_byte;
            i += 1;
        }
        else if ((first_byte & 0xE0) == 0xC0 && i + 1 < length) {
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            i += 2;
        }
        else if ((first_byte & 0xF0) == 0xE0 && i + 2 < length) {
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            output[j++] = (char)data[i + 2];
            i += 3;
        }
        else if ((first_byte & 0xF8) == 0xF0 && i + 3 < length) {
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            output[j++] = (char)data[i + 2];
            output[j++] = (char)data[i + 3];
            i += 4;
        }
        else {
            i++;
            if (j < max_output_len - 1) {
                output[j++] = '?';
            }
        }
    }
    
    if (j < max_output_len) {
        output[j] = '\0';
    } else {
        output[max_output_len - 1] = '\0';
    }
}

bool bipupu_protocol_validate_packet(const uint8_t* data, size_t length)
{
    if (!data || length < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGW(TAG, "数据包太短：%zu 字节 (需要至少 %d 字节)", 
                length, BIPUPU_MIN_PACKET_LENGTH);
        return false;
    }
    
    if (data[0] != BIPUPU_PROTOCOL_HEADER) {
        ESP_LOGW(TAG, "无效的协议头：0x%02X (期望 0x%02X)", 
                data[0], BIPUPU_PROTOCOL_HEADER);
        return false;
    }
    
    if (length >= BIPUPU_HEADER_LENGTH) {
        uint16_t data_length = read_le16(&data[6]);
        size_t expected_length = BIPUPU_HEADER_LENGTH + data_length + BIPUPU_CHECKSUM_LENGTH;
        
        if (length != expected_length) {
            ESP_LOGW(TAG, "数据包长度不匹配：实际 %zu 字节，期望 %zu 字节", 
                    length, expected_length);
            return false;
        }
        
        if (data_length > BIPUPU_MAX_DATA_LENGTH) {
            ESP_LOGW(TAG, "数据长度超出限制：%u 字节 (最大 %d 字节)", 
                    data_length, BIPUPU_MAX_DATA_LENGTH);
            return false;
        }
    }
    
    return true;
}

size_t bipupu_protocol_get_packet_length(const uint8_t* data, size_t length)
{
    if (!data || length < BIPUPU_HEADER_LENGTH) {
        return 0;
    }
    
    uint16_t data_length = read_le16(&data[6]);
    return BIPUPU_HEADER_LENGTH + data_length + BIPUPU_CHECKSUM_LENGTH;
}

bool bipupu_protocol_parse(const uint8_t* data, size_t length, bipupu_parsed_packet_t* result)
{
    if (!data || !result || length < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGE(TAG, "无效的输入参数");
        return false;
    }
    
    if (!bipupu_protocol_validate_packet(data, length)) {
        return false;
    }
    
    memset(result, 0, sizeof(bipupu_parsed_packet_t));
    
    result->header = data[0];
    result->timestamp = read_le32(&data[1]);
    result->message_type = (bipupu_message_type_t)data[5];
    result->data_length = read_le16(&data[6]);
    
    if (result->data_length > 0) {
        if (result->data_length <= BIPUPU_MAX_DATA_LENGTH) {
            memcpy(result->data, &data[BIPUPU_HEADER_LENGTH], result->data_length);
        } else {
            ESP_LOGW(TAG, "数据长度超出缓冲区限制：%u 字节", result->data_length);
            return false;
        }
    }
    
    result->checksum = data[length - 1];
    
    uint8_t calculated_checksum = bipupu_protocol_calculate_checksum(data, length - 1);
    result->checksum_valid = (calculated_checksum == result->checksum);
    
    if (!result->checksum_valid) {
        ESP_LOGW(TAG, "校验和验证失败：接收 0x%02X, 计算 0x%02X", 
                result->checksum, calculated_checksum);
        return false;
    }
    
    switch (result->message_type) {
        case BIPUPU_MSG_TEXT: {
            if (result->data_length == 0) {
                snprintf(result->sender_name, sizeof(result->sender_name), "App");
                result->body_text[0] = '\0';
                break;
            }

            uint8_t sender_len = result->data[0];

            bool sender_valid = (sender_len > 0)
                && ((size_t)(1 + sender_len) <= result->data_length)
                && (sender_len < sizeof(result->sender_name));

            if (sender_valid) {
                memcpy(result->sender_name, &result->data[1], sender_len);
                result->sender_name[sender_len] = '\0';
            } else {
                snprintf(result->sender_name, sizeof(result->sender_name), "App");
            }

            size_t body_offset = sender_valid ? (size_t)(1 + sender_len) : 1;
            size_t body_len    = result->data_length > body_offset
                                     ? result->data_length - body_offset : 0;
            bipupu_protocol_decode_utf8_safe(
                &result->data[body_offset], body_len, result->body_text);

            strncpy(result->text, result->body_text, sizeof(result->text) - 1);
            result->text[sizeof(result->text) - 1] = '\0';
            break;
        }
            
        case BIPUPU_MSG_TIME_SYNC:
            result->text[0] = '\0';
            break;
            
        case BIPUPU_MSG_ACKNOWLEDGEMENT:
            snprintf(result->text, sizeof(result->text), "[确认响应]");
            break;
            
        default:
            snprintf(result->text, sizeof(result->text), "[未知消息类型：0x%02X]", result->message_type);
            ESP_LOGW(TAG, "未知消息类型：0x%02X", result->message_type);
            break;
    }
    
    ESP_LOGI(TAG, "成功解析数据包：类型=0x%02X, 时间戳=%u, 数据长度=%u, 校验和=%s",
            result->message_type, result->timestamp, result->data_length,
            result->checksum_valid ? "有效" : "无效");
    
    return true;
}

size_t bipupu_protocol_create_time_sync(uint32_t timestamp, uint8_t* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGE(TAG, "缓冲区不足：%zu 字节 (需要至少 %d 字节)", 
                buffer_size, BIPUPU_MIN_PACKET_LENGTH);
        return 0;
    }
    
    buffer[0] = BIPUPU_PROTOCOL_HEADER;
    write_le32(&buffer[1], timestamp);
    buffer[5] = BIPUPU_MSG_TIME_SYNC;
    write_le16(&buffer[6], 0);
    
    size_t packet_length = BIPUPU_HEADER_LENGTH + BIPUPU_CHECKSUM_LENGTH;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建时间同步数据包：时间戳=%u, 长度=%zu", timestamp, packet_length);
    
    return packet_length;
}

size_t bipupu_protocol_create_binding_info(uint32_t timestamp, const char* binding_info, size_t info_length,
                                          uint8_t* buffer, size_t buffer_size)
{
    if (!binding_info || info_length == 0 || !buffer) {
        ESP_LOGE(TAG, "无效参数");
        return 0;
    }
    
    if (info_length > BIPUPU_MAX_DATA_LENGTH) {
        ESP_LOGW(TAG, "绑定信息过长：%zu 字节 (最大 %d 字节)", info_length, BIPUPU_MAX_DATA_LENGTH);
        info_length = BIPUPU_MAX_DATA_LENGTH;
    }
    
    size_t required_size = BIPUPU_HEADER_LENGTH + info_length + BIPUPU_CHECKSUM_LENGTH;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "缓冲区不足：%zu 字节 (需要 %zu 字节)", buffer_size, required_size);
        return 0;
    }
    
    buffer[0] = BIPUPU_PROTOCOL_HEADER;
    write_le32(&buffer[1], timestamp);
    buffer[5] = BIPUPU_MSG_BINDING_INFO;
    write_le16(&buffer[6], (uint16_t)info_length);
    
    if (info_length > 0) {
        memcpy(&buffer[BIPUPU_HEADER_LENGTH], binding_info, info_length);
    }
    
    size_t packet_length = BIPUPU_HEADER_LENGTH + info_length + BIPUPU_CHECKSUM_LENGTH;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建绑定信息数据包：时间戳=%u, 信息长度=%zu, 总长度=%zu", 
            timestamp, info_length, packet_length);
    
    return packet_length;
}

size_t bipupu_protocol_create_unbind_confirm(uint32_t timestamp, uint8_t* buffer, size_t buffer_size)
{
    if (!buffer) {
        ESP_LOGE(TAG, "无效参数");
        return 0;
    }
    
    size_t required_size = BIPUPU_HEADER_LENGTH + BIPUPU_CHECKSUM_LENGTH;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "缓冲区不足：%zu 字节 (需要 %zu 字节)", buffer_size, required_size);
        return 0;
    }
    
    buffer[0] = BIPUPU_PROTOCOL_HEADER;
    write_le32(&buffer[1], timestamp);
    buffer[5] = BIPUPU_MSG_UNBIND_COMMAND;
    write_le16(&buffer[6], 0);
    
    size_t packet_length = BIPUPU_HEADER_LENGTH + BIPUPU_CHECKSUM_LENGTH;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建解绑确认数据包：时间戳=%u, 总长度=%zu", timestamp, packet_length);
    
    return packet_length;
}

size_t bipupu_protocol_create_acknowledgement(uint32_t original_message_id, uint8_t* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGE(TAG, "缓冲区不足：%zu 字节 (需要至少 %d 字节)", 
                buffer_size, BIPUPU_MIN_PACKET_LENGTH);
        return 0;
    }
    
    const size_t data_length = 4;
    size_t required_size = BIPUPU_HEADER_LENGTH + data_length + BIPUPU_CHECKSUM_LENGTH;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "缓冲区不足：%zu 字节 (需要 %zu 字节)", buffer_size, required_size);
        return 0;
    }
    
    buffer[0] = BIPUPU_PROTOCOL_HEADER;
    write_le32(&buffer[1], original_message_id);
    buffer[5] = BIPUPU_MSG_ACKNOWLEDGEMENT;
    write_le16(&buffer[6], data_length);
    
    write_le32(&buffer[BIPUPU_HEADER_LENGTH], original_message_id);
    
    size_t packet_length = required_size;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建 ACK 数据包：msg_id=%u, 长度=%zu", original_message_id, packet_length);
    
    return packet_length;
}
