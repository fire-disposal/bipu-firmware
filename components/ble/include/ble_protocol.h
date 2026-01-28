#pragma once

#include <stdint.h>
#include <stddef.h>

/* ================== 协议定义 ================== */
#define PROTOCOL_VERSION          0x01
#define CMD_TYPE_MESSAGE          0x01

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
