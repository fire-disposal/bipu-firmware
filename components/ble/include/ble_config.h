#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================== BLE配置常量 ================== */
#define BLE_DEVICE_NAME           "BIPUPU——test1"
#define BLE_MAX_MESSAGE_LEN       128
#define BLE_ADV_INTERVAL_MIN      0x20
#define BLE_ADV_INTERVAL_MAX      0x40

/* ================== UUID 定义 ================== */
// Bipupu Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_SERVICE_UUID_128   {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}
// Command Input: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_CHAR_CMD_UUID_128  {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}
// Status Output: 6E400004-B5A3-F393-E0A9-E50E24DCCA9E
#define BIPUPU_CHAR_STATUS_UUID_128 {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x04, 0x00, 0x40, 0x6E}

// Battery Service: 0x180F
#define BATTERY_SERVICE_UUID      0x180F
// Battery Level: 0x2A19
#define BATTERY_LEVEL_UUID        0x2A19

// Current Time Service (CTS): 0x180A
#define CTS_SERVICE_UUID          0x180A
// Current Time: 0x2A2B (读写，用于接收时间更新)
#define CTS_CURRENT_TIME_UUID     0x2A2B
// Local Time Info: 0x2A0F (可选，用于时区信息)
#define CTS_LOCAL_TIME_INFO_UUID  0x2A0F

/* ================== 协议配置 ================== */
#define PROTOCOL_VERSION          0x01
#define CMD_TYPE_MESSAGE          0x01

#ifdef __cplusplus
}
#endif