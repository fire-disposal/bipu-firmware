#pragma once
#include "esp_err.h"
#include <stddef.h>
#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t storage_init(void);
esp_err_t storage_nvs_init(void);

esp_err_t storage_save_messages(const ui_message_t* msgs, int count, int current_idx);
esp_err_t storage_load_messages(ui_message_t* msgs, int* out_count, int* out_current_idx);

esp_err_t storage_save_ble_addr(const char* addr);
esp_err_t storage_load_ble_addr(char* buf, size_t buf_len);

esp_err_t storage_save_brightness(uint8_t brightness);
esp_err_t storage_load_brightness(uint8_t* out_brightness);

#ifdef __cplusplus
}
#endif
