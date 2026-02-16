#ifndef APP_BLE_H
#define APP_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BLE消息接收回调函数
 * @param sender 发送者标识
 * @param message 消息内容
 */
void ble_message_received(const char* sender, const char* message);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_H