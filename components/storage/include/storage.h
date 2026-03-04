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
esp_err_t storage_nvs_init(void);

esp_err_t storage_save_messages(const storage_message_t* msgs, int count, int current_idx);
esp_err_t storage_load_messages(storage_message_t* msgs, int* out_count, int* out_current_idx);

esp_err_t storage_save_ble_addr(const char* addr);
esp_err_t storage_load_ble_addr(char* buf, size_t buf_len);

esp_err_t storage_save_brightness(uint8_t brightness);
esp_err_t storage_load_brightness(uint8_t* out_brightness);

#ifdef __cplusplus
}
#endif
