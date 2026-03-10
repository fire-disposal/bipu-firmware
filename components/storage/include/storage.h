#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 消息存储层数据结构（独立于 UI 层）
 * ui_types.h 中的 ui_message_t 是此类型的别名，上层代码无需修改。
 */
#define MAX_MESSAGES 10

typedef struct {
    char sender[32];
    char text[128];
    uint32_t timestamp;
    bool is_read;
} storage_message_t;

esp_err_t storage_init(void);

esp_err_t storage_save_messages(const storage_message_t* msgs, int count, int current_idx);
esp_err_t storage_load_messages(storage_message_t* msgs, int* out_count, int* out_current_idx);

esp_err_t storage_save_ble_addr(const char* addr);
esp_err_t storage_load_ble_addr(char* buf, size_t buf_len);

esp_err_t storage_save_brightness(uint8_t brightness);
esp_err_t storage_load_brightness(uint8_t* out_brightness);

/**
 * @brief 保存上次 BLE 时间同步的时间戳和系统计时器值
 * @param timestamp Unix 时间戳 (秒)
 * @param esp_timer_us esp_timer_get_time() 的值 (微秒)
 * @return ESP_OK 成功
 */
esp_err_t storage_save_time_sync(uint32_t timestamp, uint64_t esp_timer_us);

/**
 * @brief 加载上次 BLE 时间同步的数据
 * @param out_timestamp 输出：Unix 时间戳
 * @param out_esp_timer_us 输出：esp_timer_get_time() 的值
 * @return ESP_OK 成功，ESP_ERR_NVS_NOT_FOUND 表示无同步记录（首次启动）
 */
esp_err_t storage_load_time_sync(uint32_t* out_timestamp, uint64_t* out_esp_timer_us);

#ifdef __cplusplus
}
#endif
