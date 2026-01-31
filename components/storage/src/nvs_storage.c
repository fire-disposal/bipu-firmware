#include "esp_err.h"
#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "ui_types.h"
#include <string.h>

static const char* TAG = "storage";
static const char* NAMESPACE = "bipi";

esp_err_t storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t storage_save_messages(const ui_message_t* msgs, int count, int current_idx) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_i32(h, "msg_count", count);
    if (err != ESP_OK) goto _close;
    err = nvs_set_i32(h, "current_idx", current_idx);
    if (err != ESP_OK) goto _close;

    for (int i = 0; i < count && i < MAX_MESSAGES; i++) {
        char key[32];
        // sender
        snprintf(key, sizeof(key), "m%d_s", i);
        err = nvs_set_str(h, key, msgs[i].sender);
        if (err != ESP_OK) goto _close;
        // text
        snprintf(key, sizeof(key), "m%d_t", i);
        err = nvs_set_str(h, key, msgs[i].text);
        if (err != ESP_OK) goto _close;
        // timestamp
        snprintf(key, sizeof(key), "m%d_ts", i);
        err = nvs_set_u32(h, key, msgs[i].timestamp);
        if (err != ESP_OK) goto _close;
        // is_read
        snprintf(key, sizeof(key), "m%d_r", i);
        err = nvs_set_u8(h, key, msgs[i].is_read ? 1 : 0);
        if (err != ESP_OK) goto _close;
    }

    err = nvs_commit(h);

_close:
    nvs_close(h);
    return err;
}

esp_err_t storage_load_messages(ui_message_t* msgs, int* out_count, int* out_current_idx) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // If no NVS namespace yet, treat as empty
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *out_count = 0;
            *out_current_idx = 0;
            return ESP_OK;
        }
        return err;
    }

    int32_t msg_count = 0;
    int32_t cur_idx = 0;
    err = nvs_get_i32(h, "msg_count", &msg_count);
    if (err != ESP_OK) {
        msg_count = 0;
    }
    err = nvs_get_i32(h, "current_idx", &cur_idx);
    if (err != ESP_OK) {
        cur_idx = 0;
    }

    int load_count = msg_count;
    if (load_count > MAX_MESSAGES) load_count = MAX_MESSAGES;
    for (int i = 0; i < load_count; i++) {
        char key[32];
        size_t required = 0;
        // sender
        snprintf(key, sizeof(key), "m%d_s", i);
        err = nvs_get_str(h, key, NULL, &required);
        if (err == ESP_OK && required > 0) {
            if (required > sizeof(msgs[i].sender)) required = sizeof(msgs[i].sender);
            nvs_get_str(h, key, msgs[i].sender, &required);
        } else {
            msgs[i].sender[0] = '\0';
        }
        // text
        snprintf(key, sizeof(key), "m%d_t", i);
        required = 0;
        err = nvs_get_str(h, key, NULL, &required);
        if (err == ESP_OK && required > 0) {
            if (required > sizeof(msgs[i].text)) required = sizeof(msgs[i].text);
            nvs_get_str(h, key, msgs[i].text, &required);
        } else {
            msgs[i].text[0] = '\0';
        }
        // timestamp
        snprintf(key, sizeof(key), "m%d_ts", i);
        uint32_t ts = 0;
        if (nvs_get_u32(h, key, &ts) == ESP_OK) msgs[i].timestamp = ts;
        else msgs[i].timestamp = 0;
        // is_read
        snprintf(key, sizeof(key), "m%d_r", i);
        uint8_t r = 0;
        if (nvs_get_u8(h, key, &r) == ESP_OK) msgs[i].is_read = (r != 0);
        else msgs[i].is_read = false;
    }

    *out_count = load_count;
    *out_current_idx = cur_idx;

    nvs_close(h);
    return ESP_OK;
}

esp_err_t storage_save_ble_addr(const char* addr) {
    if (!addr) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ble_addr", addr);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t storage_load_ble_addr(char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required = buf_len;
    err = nvs_get_str(h, "ble_addr", buf, &required);
    nvs_close(h);
    return err;
}
