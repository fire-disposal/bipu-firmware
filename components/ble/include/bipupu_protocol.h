/**
 * @file bipupu_protocol.h
 * @brief Bipupu 蓝牙协议解析接口 (版本 1.2)
 * 
 * 协议格式:
 * [协议头(1字节)][时间戳(4字节)][消息类型(1字节)][数据长度(2字节)][数据(N字节)][校验和(1字节)]
 * 
 * 协议头: 0xB0 (固定值)
 * 字节序: 小端序
 * 校验和: 异或校验 (XOR)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 协议常量定义 ================== */

/** 协议头固定值 */
#define BIPUPU_PROTOCOL_HEADER 0xB0

/** 最大数据长度 (受蓝牙MTU限制) */
#define BIPUPU_MAX_DATA_LENGTH 240

/** 头部长度 (协议头 + 时间戳 + 消息类型 + 数据长度) */
#define BIPUPU_HEADER_LENGTH 8

/** 校验和长度 */
#define BIPUPU_CHECKSUM_LENGTH 1

/** 最小数据包长度 */
#define BIPUPU_MIN_PACKET_LENGTH (BIPUPU_HEADER_LENGTH + BIPUPU_CHECKSUM_LENGTH)

/* ================== 消息类型定义 ================== */

/** 消息类型枚举 */
typedef enum {
    BIPUPU_MSG_TIME_SYNC = 0x01,      /**< 时间同步消息 */
    BIPUPU_MSG_TEXT = 0x02,           /**< 文本消息 */
    BIPUPU_MSG_ACKNOWLEDGEMENT = 0x03 /**< 确认响应 (预留) */
} bipupu_message_type_t;

/* ================== 解析结果结构 ================== */

/** 解析后的数据包结构 */
typedef struct {
    uint8_t header;                     /**< 协议头 (应为 0xB0) */
    uint32_t timestamp;                 /**< Unix时间戳 (秒) */
    bipupu_message_type_t message_type; /**< 消息类型 */
    uint16_t data_length;               /**< 数据部分长度 */
    uint8_t data[BIPUPU_MAX_DATA_LENGTH]; /**< 原始数据 */
    uint8_t checksum;                   /**< 接收到的校验和 */
    bool checksum_valid;                /**< 校验和是否有效 */
    char text[BIPUPU_MAX_DATA_LENGTH + 1]; /**< UTF-8解码后的文本 (以null结尾) */
} bipupu_parsed_packet_t;

/* ================== 协议解析接口 ================== */

/**
 * @brief 解析Bipupu蓝牙数据包
 * 
 * @param data 接收到的原始数据
 * @param length 数据长度
 * @param result 解析结果输出
 * @return true 解析成功，false 解析失败
 */
bool bipupu_protocol_parse(const uint8_t* data, size_t length, bipupu_parsed_packet_t* result);

/**
 * @brief 计算数据包的校验和
 * 
 * @param data 数据指针
 * @param length 数据长度 (不包括校验和本身)
 * @return uint8_t 计算出的校验和
 */
uint8_t bipupu_protocol_calculate_checksum(const uint8_t* data, size_t length);

/**
 * @brief 安全的UTF-8解码函数
 * 
 * @param data UTF-8编码的字节数组
 * @param length 数据长度
 * @param output 输出缓冲区 (确保足够大，建议BIPUPU_MAX_DATA_LENGTH+1)
 */
void bipupu_protocol_decode_utf8_safe(const uint8_t* data, size_t length, char* output);

/**
 * @brief 创建时间同步数据包
 * 
 * @param timestamp Unix时间戳 (秒)
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return size_t 实际写入的字节数，0表示失败
 */
size_t bipupu_protocol_create_time_sync(uint32_t timestamp, uint8_t* buffer, size_t buffer_size);

/**
 * @brief 创建文本消息数据包
 * 
 * @param timestamp Unix时间戳 (秒)
 * @param text UTF-8编码的文本
 * @param text_length 文本长度 (字节数)
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return size_t 实际写入的字节数，0表示失败
 */
size_t bipupu_protocol_create_text_message(uint32_t timestamp, const char* text, size_t text_length, 
                                          uint8_t* buffer, size_t buffer_size);

/**
 * @brief 验证数据包的基本有效性
 * 
 * @param data 数据指针
 * @param length 数据长度
 * @return true 数据包格式基本有效，false 无效
 */
bool bipupu_protocol_validate_packet(const uint8_t* data, size_t length);

/**
 * @brief 获取数据包的总长度 (根据数据长度字段计算)
 * 
 * @param data 数据指针 (至少包含头部)
 * @param length 可用数据长度
 * @return size_t 预期的总数据包长度，0表示无法确定
 */
size_t bipupu_protocol_get_packet_length(const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif