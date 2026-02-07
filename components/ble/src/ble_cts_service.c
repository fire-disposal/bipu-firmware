#include "ble_cts_service.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "ble_protocol.h"
#include <string.h>

static const char* CTS_TAG = "ble_cts_service";

static ble_cts_service_handles_t s_handles = {0};
static ble_cts_time_callback_t s_time_callback = NULL;

/* UUIDs */
static const uint16_t s_cts_service_uuid = CTS_SERVICE_UUID;
static const uint16_t s_cts_time_uuid = CTS_CURRENT_TIME_UUID;
static const uint16_t s_cts_local_time_uuid = CTS_LOCAL_TIME_INFO_UUID;

/* 处理 CTS 时间写入 */
static void handle_cts_write(const uint8_t *data, uint16_t len) {
    if (!data || len < 10) {
        ESP_LOGE(CTS_TAG, "CTS data length insufficient: %d (need at least 10 bytes)", len);
        return;
    }

    ESP_LOGI(CTS_TAG, "Received CTS time write request, data length: %d", len);

    ble_cts_time_t cts_time;
    if (ble_protocol_parse_cts_time(data, len, &cts_time)) {
        ESP_LOGI(CTS_TAG, "CTS time sync success - Date: %04d-%02d-%02d Time: %02d:%02d:%02d (Weekday %d, Adjust reason: 0x%02X)",
                 cts_time.year, cts_time.month, cts_time.day,
                 cts_time.hour, cts_time.minute, cts_time.second,
                 cts_time.weekday, cts_time.adjust_reason);

        if (s_time_callback) {
            s_time_callback(&cts_time);
        }

        ESP_LOGI(CTS_TAG, "CTS time sync processing completed");
    } else {
        ESP_LOGE(CTS_TAG, "CTS time data parsing failed");
    }
}

esp_err_t ble_cts_service_init(esp_gatt_if_t gatts_if, ble_cts_service_handles_t* handles) {
    if (!handles) return ESP_ERR_INVALID_ARG;

    esp_gatt_srvc_id_t service_id;
    service_id.is_primary = true;
    service_id.id.inst_id = 2;
    service_id.id.uuid.len = ESP_UUID_LEN_16;
    service_id.id.uuid.uuid.uuid16 = s_cts_service_uuid;

    esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(CTS_TAG, "Failed to create CTS service: %s", esp_err_to_name(ret));
        return ret;
    }

    *handles = s_handles;
    return ESP_OK;
}

void ble_cts_service_deinit(void) {
    if (s_handles.service_handle != 0) {
        esp_ble_gatts_delete_service(s_handles.service_handle);
        s_handles.service_handle = 0;
        s_handles.time_char_handle = 0;
        s_handles.local_time_char_handle = 0;
    }
    s_time_callback = NULL;
}

void ble_cts_service_set_time_callback(ble_cts_time_callback_t callback) {
    s_time_callback = callback;
}

void ble_cts_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_CREATE_EVT:
            if (param->create.service_id.id.inst_id == 2) {
                s_handles.service_handle = param->create.service_handle;
                esp_ble_gatts_start_service(s_handles.service_handle);

                // Add Current Time characteristic
                esp_bt_uuid_t cts_time_uuid;
                cts_time_uuid.len = ESP_UUID_LEN_16;
                cts_time_uuid.uuid.uuid16 = s_cts_time_uuid;

                esp_attr_value_t cts_time_val = { .attr_max_len = 10, .attr_len = 0, .attr_value = NULL };
                esp_ble_gatts_add_char(s_handles.service_handle, &cts_time_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                     &cts_time_val, NULL);

                // Add Local Time Info characteristic
                esp_bt_uuid_t cts_local_time_uuid;
                cts_local_time_uuid.len = ESP_UUID_LEN_16;
                cts_local_time_uuid.uuid.uuid16 = s_cts_local_time_uuid;

                esp_attr_value_t cts_local_time_val = { .attr_max_len = 2, .attr_len = 0, .attr_value = NULL };
                esp_ble_gatts_add_char(s_handles.service_handle, &cts_local_time_uuid,
                                     ESP_GATT_PERM_READ,
                                     ESP_GATT_CHAR_PROP_BIT_READ,
                                     &cts_local_time_val, NULL);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.len == ESP_UUID_LEN_16) {
                if (param->add_char.char_uuid.uuid.uuid16 == s_cts_time_uuid) {
                    s_handles.time_char_handle = param->add_char.attr_handle;
                    // Add CCCD for Notify
                    esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
                    esp_attr_value_t cccd_val = { .attr_max_len = 2, .attr_len = 2, .attr_value = (uint8_t[]){0x00, 0x00} };
                    esp_ble_gatts_add_char_descr(s_handles.service_handle, &cccd_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_val, NULL);
                } else if (param->add_char.char_uuid.uuid.uuid16 == s_cts_local_time_uuid) {
                    s_handles.local_time_char_handle = param->add_char.attr_handle;
                }
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == s_handles.time_char_handle) {
                handle_cts_write(param->write.value, param->write.len);
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;

        default:
            break;
    }
}