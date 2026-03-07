#include "esp_err.h"
#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "storage";
static const char* NAMESPACE = "bipi";

esp_err_t storage_init(void) {
    /* NVS 子系统由 main.c 的 init_nvs() 统一初始化。
     * 此函数仅验证存储命名空间可访问，不再重复调用 nvs_flash_init()
     * （避免重复初始化产生的警告，以及边缘情况下误触发 nvs_flash_erase）。 */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* 命名空间尚不存在（首次运行），属于正常情况 */
        ESP_LOGI(TAG, "NVS namespace not found yet, will be created on first write");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage_init: failed to open NVS namespace '%s': %s",
                 NAMESPACE, esp_err_to_name(err));
        return err;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "NVS storage namespace ready");
    return ESP_OK;
}

/**
 * @brief 将消息数组以 blob 方式序列化存储
 *
 * 旧格式使用每条消息4个独立 key（m0_s/m0_t/m0_ts/m0_r × 10条 = 40+个key），
 * 新格式使用3个key（msg_count + cur_idx + msgs_data blob），
 * 大幅减少 NVS 写入次数，降低 Flash 磨损。
 */
esp_err_t storage_save_messages(const storage_message_t* msgs, int count, int current_idx) {
    if (!msgs || count < 0 || count > MAX_MESSAGES) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_i32(h, "msg_count", count);
    if (err != ESP_OK) goto _close;

    err = nvs_set_i32(h, "cur_idx", current_idx);
    if (err != ESP_OK) goto _close;

    if (count > 0) {
        err = nvs_set_blob(h, "msgs_data", msgs, sizeof(storage_message_t) * (size_t)count);
        if (err != ESP_OK) goto _close;
    }

    err = nvs_commit(h);

_close:
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage_save_messages failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_load_messages(storage_message_t* msgs, int* out_count, int* out_current_idx) {
    if (!msgs || !out_count || !out_current_idx) return ESP_ERR_INVALID_ARG;

    *out_count = 0;
    *out_current_idx = 0;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // 命名空间不存在视为空存储，属于正常情况
        if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
        return err;
    }

    int32_t msg_count = 0;
    int32_t cur_idx = 0;
    nvs_get_i32(h, "msg_count", &msg_count);
    nvs_get_i32(h, "cur_idx", &cur_idx);

    // 边界校验
    if (msg_count < 0 || msg_count > MAX_MESSAGES) {
        ESP_LOGW(TAG, "Invalid msg_count=%ld in NVS, resetting to 0", msg_count);
        msg_count = 0;
    }
    if (cur_idx < 0 || (msg_count > 0 && cur_idx >= msg_count)) {
        cur_idx = 0;
    }

    if (msg_count > 0) {
        size_t sz = sizeof(storage_message_t) * (size_t)msg_count;
        err = nvs_get_blob(h, "msgs_data", msgs, &sz);
        if (err != ESP_OK) {
            // blob 读取失败（可能是从旧格式升级），丢弃历史消息，从零开始
            ESP_LOGW(TAG, "msgs_data blob read failed (%s), discarding old messages",
                     esp_err_to_name(err));
            msg_count = 0;
            cur_idx = 0;
            err = ESP_OK; // 非致命错误
        }
    }

    *out_count = (int)msg_count;
    *out_current_idx = (int)cur_idx;

    nvs_close(h);
    return err;
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

esp_err_t storage_save_brightness(uint8_t brightness) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "brightness", brightness);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Brightness saved: %d", brightness);
    return err;
}

esp_err_t storage_load_brightness(uint8_t* out_brightness) {
    if (!out_brightness) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *out_brightness = 100;
            return ESP_OK;
        }
        return err;
    }
    err = nvs_get_u8(h, "brightness", out_brightness);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_brightness = 100;
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

