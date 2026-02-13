/**
 * @file ble_config.h
 * @brief BLE 配置常量和 UUID 定义 (NimBLE 版本)
 * 
 * 本文件定义了 BLE 服务的所有配置参数，包括：
 * - Nordic UART Service (NUS) UUID
 * - Battery Service UUID
 * - Current Time Service (CTS) UUID
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================== 设备配置 ================== */
#define BLE_DEVICE_NAME           "BIPUPU"
#define BLE_MAX_MESSAGE_LEN       512       // 支持较长消息（分块传输）
#define BLE_MTU_SIZE              247       // 请求的 MTU 大小
#define BLE_CHUNK_SIZE            20        // 默认分块大小 (MTU-3)

/* ================== 广播参数 ================== */
#define BLE_ADV_INTERVAL_MIN      0xA0      // 100ms
#define BLE_ADV_INTERVAL_MAX      0xF0      // 150ms

/* ================== 连接参数 ================== */
#define BLE_CONN_INT_MIN          0x18      // 30ms
#define BLE_CONN_INT_MAX          0x30      // 60ms
#define BLE_CONN_LATENCY          4         // 可跳过4个间隔再响应，降低射频活动
#define BLE_CONN_TIMEOUT          400       // 4s

/* ================== Nordic UART Service (NUS) UUID ================== */
// 服务 UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
#define NUS_SERVICE_UUID_128      {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
                                   0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}

// TX 特征 (手机写入): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
#define NUS_CHAR_TX_UUID_128      {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
                                   0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E}

// RX 特征 (设备发送): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
#define NUS_CHAR_RX_UUID_128      {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
                                   0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}

// NimBLE 使用字符串格式的 UUID
#define NUS_SERVICE_UUID_STR      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_TX_UUID_STR      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID_STR      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

/* ================== Current Time Service (CTS, 标准 BLE 服务) ================== */
// 服务 UUID: 0x1805
#define CTS_SERVICE_UUID          0x1805
// Current Time 特征: 0x2A2B
#define CTS_CURRENT_TIME_UUID     0x2A2B
// Local Time Info 特征: 0x2A0F (可选)
#define CTS_LOCAL_TIME_INFO_UUID  0x2A0F

#ifdef __cplusplus
}
#endif