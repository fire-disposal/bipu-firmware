/**
 * @file bipupu_protocol.c
 * @brief Bipupu 蓝牙协议解析实现 (版本 1.2)
 * 
 * 协议格式:
 * [协议头(1字节)][时间戳(4字节)][消息类型(1字节)][数据长度(2字节)][数据(N字节)][校验和(1字节)]
 * 
 * 协议头: 0xB0 (固定值)
 * 字节序: 小端序
 * 校验和: 异或校验 (XOR)
 */

#include "bipupu_protocol.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "bipupu_protocol";

/* ================== 内部辅助函数 ================== */

/**
 * @brief 从小端字节序读取16位整数
 */
static uint16_t read_le16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/**
 * @brief 从小端字节序读取32位整数
 */
static uint32_t read_le32(const uint8_t* data)
{
    return (uint32_t)data[0] | 
           ((uint32_t)data[1] << 8) | 
           ((uint32_t)data[2] << 16) | 
           ((uint32_t)data[3] << 24);
}

/**
 * @brief 写入16位整数为小端字节序
 */
static void write_le16(uint8_t* buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
}

/**
 * @brief 写入32位整数为小端字节序
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
            // ASCII字符 (0xxxxxxx)
            output[j++] = (char)first_byte;
            i += 1;
        }
        else if ((first_byte & 0xE0) == 0xC0 && i + 1 < length) {
            // 2字节字符 (110xxxxx)
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            i += 2;
        }
        else if ((first_byte & 0xF0) == 0xE0 && i + 2 < length) {
            // 3字节字符 (1110xxxx) - 大部分中文字符
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            output[j++] = (char)data[i + 2];
            i += 3;
        }
        else if ((first_byte & 0xF8) == 0xF0 && i + 3 < length) {
            // 4字节字符 (11110xxx) - Emoji等
            output[j++] = (char)first_byte;
            output[j++] = (char)data[i + 1];
            output[j++] = (char)data[i + 2];
            output[j++] = (char)data[i + 3];
            i += 4;
        }
        else {
            // 无效的UTF-8序列，跳过或替换
            i++;
            if (j < max_output_len - 1) {
                output[j++] = '?';
            }
        }
    }
    
    // 确保以null结尾
    if (j < max_output_len) {
        output[j] = '\0';
    } else {
        output[max_output_len - 1] = '\0';
    }
}

bool bipupu_protocol_validate_packet(const uint8_t* data, size_t length)
{
    if (!data || length < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGW(TAG, "数据包太短: %zu 字节 (需要至少 %d 字节)", 
                length, BIPUPU_MIN_PACKET_LENGTH);
        return false;
    }
    
    // 检查协议头
    if (data[0] != BIPUPU_PROTOCOL_HEADER) {
        ESP_LOGW(TAG, "无效的协议头: 0x%02X (期望 0x%02X)", 
                data[0], BIPUPU_PROTOCOL_HEADER);
        return false;
    }
    
    // 检查数据长度字段
    if (length >= BIPUPU_HEADER_LENGTH) {
        uint16_t data_length = read_le16(&data[6]);
        size_t expected_length = BIPUPU_HEADER_LENGTH + data_length + BIPUPU_CHECKSUM_LENGTH;
        
        if (length != expected_length) {
            ESP_LOGW(TAG, "数据包长度不匹配: 实际 %zu 字节, 期望 %zu 字节", 
                    length, expected_length);
            return false;
        }
        
        if (data_length > BIPUPU_MAX_DATA_LENGTH) {
            ESP_LOGW(TAG, "数据长度超出限制: %u 字节 (最大 %d 字节)", 
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
    
    // 读取数据长度字段
    uint16_t data_length = read_le16(&data[6]);
    return BIPUPU_HEADER_LENGTH + data_length + BIPUPU_CHECKSUM_LENGTH;
}

bool bipupu_protocol_parse(const uint8_t* data, size_t length, bipupu_parsed_packet_t* result)
{
    // 基本验证
    if (!data || !result || length < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGE(TAG, "无效的输入参数");
        return false;
    }
    
    // 验证数据包格式
    if (!bipupu_protocol_validate_packet(data, length)) {
        return false;
    }
    
    // 清空结果结构
    memset(result, 0, sizeof(bipupu_parsed_packet_t));
    
    // 解析协议头
    result->header = data[0];
    
    // 解析时间戳 (小端序)
    result->timestamp = read_le32(&data[1]);
    
    // 解析消息类型
    result->message_type = (bipupu_message_type_t)data[5];
    
    // 解析数据长度 (小端序)
    result->data_length = read_le16(&data[6]);
    
    // 提取数据
    if (result->data_length > 0) {
        if (result->data_length <= BIPUPU_MAX_DATA_LENGTH) {
            memcpy(result->data, &data[BIPUPU_HEADER_LENGTH], result->data_length);
        } else {
            ESP_LOGW(TAG, "数据长度超出缓冲区限制: %u 字节", result->data_length);
            return false;
        }
    }
    
    // 提取校验和
    result->checksum = data[length - 1];
    
    // 计算并验证校验和
    uint8_t calculated_checksum = bipupu_protocol_calculate_checksum(data, length - 1);
    result->checksum_valid = (calculated_checksum == result->checksum);
    
    if (!result->checksum_valid) {
        ESP_LOGW(TAG, "校验和验证失败: 接收 0x%02X, 计算 0x%02X", 
                result->checksum, calculated_checksum);
        return false;
    }
    
    // 根据消息类型处理数据
    switch (result->message_type) {
        case BIPUPU_MSG_TEXT:
            // UTF-8解码文本
            bipupu_protocol_decode_utf8_safe(result->data, result->data_length, result->text);
            break;
            
        case BIPUPU_MSG_TIME_SYNC:
            // 时间同步消息，数据通常为空
            result->text[0] = '\0';
            break;
            
        case BIPUPU_MSG_ACKNOWLEDGEMENT:
            // 确认响应消息
            snprintf(result->text, sizeof(result->text), "[确认响应]");
            break;
            
        default:
            // 未知消息类型
            snprintf(result->text, sizeof(result->text), "[未知消息类型: 0x%02X]", result->message_type);
            ESP_LOGW(TAG, "未知消息类型: 0x%02X", result->message_type);
            break;
    }
    
    ESP_LOGI(TAG, "成功解析数据包: 类型=0x%02X, 时间戳=%u, 数据长度=%u, 校验和=%s",
            result->message_type, result->timestamp, result->data_length,
            result->checksum_valid ? "有效" : "无效");
    
    return true;
}

size_t bipupu_protocol_create_time_sync(uint32_t timestamp, uint8_t* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < BIPUPU_MIN_PACKET_LENGTH) {
        ESP_LOGE(TAG, "缓冲区不足: %zu 字节 (需要至少 %d 字节)", 
                buffer_size, BIPUPU_MIN_PACKET_LENGTH);
        return 0;
    }
    
    // 构建数据包
    buffer[0] = BIPUPU_PROTOCOL_HEADER;  // 协议头
    write_le32(&buffer[1], timestamp);   // 时间戳
    buffer[5] = BIPUPU_MSG_TIME_SYNC;    // 消息类型
    write_le16(&buffer[6], 0);           // 数据长度 = 0
    
    // 计算校验和 (不包括校验和本身)
    size_t packet_length = BIPUPU_HEADER_LENGTH + BIPUPU_CHECKSUM_LENGTH;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建时间同步数据包: 时间戳=%u, 长度=%zu", timestamp, packet_length);
    
    return packet_length;
}

size_t bipupu_protocol_create_text_message(uint32_t timestamp, const char* text, 
                                          size_t text_length, uint8_t* buffer, 
                                          size_t buffer_size)
{
    if (!text || !buffer) {
        ESP_LOGE(TAG, "无效的输入参数");
        return 0;
    }
    
    // 限制文本长度
    if (text_length > BIPUPU_MAX_DATA_LENGTH) {
        ESP_LOGW(TAG, "文本长度超出限制: %zu 字节, 截断为 %d 字节", 
                text_length, BIPUPU_MAX_DATA_LENGTH);
        text_length = BIPUPU_MAX_DATA_LENGTH;
    }
    
    // 计算所需缓冲区大小
    size_t required_size = BIPUPU_HEADER_LENGTH + text_length + BIPUPU_CHECKSUM_LENGTH;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "缓冲区不足: %zu 字节 (需要 %zu 字节)", buffer_size, required_size);
        return 0;
    }
    
    // 构建数据包
    buffer[0] = BIPUPU_PROTOCOL_HEADER;  // 协议头
    write_le32(&buffer[1], timestamp);   // 时间戳
    buffer[5] = BIPUPU_MSG_TEXT;         // 消息类型
    write_le16(&buffer[6], (uint16_t)text_length); // 数据长度
    
    // 复制文本数据
    if (text_length > 0) {
        memcpy(&buffer[BIPUPU_HEADER_LENGTH], text, text_length);
    }
    
    // 计算校验和 (不包括校验和本身)
    size_t packet_length = BIPUPU_HEADER_LENGTH + text_length + BIPUPU_CHECKSUM_LENGTH;
    uint8_t checksum = bipupu_protocol_calculate_checksum(buffer, packet_length - 1);
    buffer[packet_length - 1] = checksum;
    
    ESP_LOGI(TAG, "创建文本消息数据包: 时间戳=%u, 文本长度=%zu, 总长度=%zu", 
            timestamp, text_length, packet_length);
    
    return packet_length;
}