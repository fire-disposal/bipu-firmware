#include "ble_bipupu_service.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "ble_protocol.h"
#include <string.h>

static const char* BIPUPU_TAG = "ble_bipupu_service";

static ble_bipupu_service_handles_t s_handles = {0};
static ble_bipupu_message_callback_t s_message_callback = NULL;

/* UUIDs */
static const uint8_t s_bipupu_service_uuid[16] = BIPUPU_SERVICE_UUID_128;
static const uint8_t s_cmd_char_uuid[16] = BIPUPU_CHAR_CMD_UUID_128;
static const uint8_t s_status_char_uuid[16] = BIPUPU_CHAR_STATUS_UUID_128;

/* 协议处理 */
static void handle_write_data(const uint8_t *data, uint16_t len) {
    if (!data || len == 0) return;

    ble_parsed_msg_t msg;
    if (ble_protocol_parse(data, len, &msg)) {
        if (s_message_callback) {
            s_message_callback(msg.sender, msg.message, &msg.effect);
        }
    }
}

esp_err_t ble_bipupu_service_init(esp_gatt_if_t gatts_if, ble_bipupu_service_handles_t* handles) {
    if (!handles) return ESP_ERR_INVALID_ARG;

    esp_gatt_srvc_id_t service_id;
    service_id.is_primary = true;
    service_id.id.inst_id = 0;
    service_id.id.uuid.len = ESP_UUID_LEN_128;
    memcpy(service_id.id.uuid.uuid.uuid128, s_bipupu_service_uuid, ESP_UUID_LEN_128);

    esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(BIPUPU_TAG, "Failed to create Bipupu service: %s", esp_err_to_name(ret));
        return ret;
    }

    *handles = s_handles;
    return ESP_OK;
}

void ble_bipupu_service_deinit(void) {
    if (s_handles.service_handle != 0) {
        esp_ble_gatts_delete_service(s_handles.service_handle);
        s_handles.service_handle = 0;
        s_handles.cmd_char_handle = 0;
        s_handles.status_char_handle = 0;
    }
    s_message_callback = NULL;
}

void ble_bipupu_service_set_message_callback(ble_bipupu_message_callback_t callback) {
    s_message_callback = callback;
}

void ble_bipupu_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_CREATE_EVT:
            if (param->create.service_id.id.inst_id == 0) {
                s_handles.service_handle = param->create.service_handle;
                esp_ble_gatts_start_service(s_handles.service_handle);

                // Add Command Input
                esp_bt_uuid_t cmd_uuid;
                cmd_uuid.len = ESP_UUID_LEN_128;
                memcpy(cmd_uuid.uuid.uuid128, s_cmd_char_uuid, ESP_UUID_LEN_128);

                esp_attr_value_t cmd_val = { .attr_max_len = BLE_MAX_MESSAGE_LEN, .attr_len = 0, .attr_value = NULL };
                esp_ble_gatts_add_char(s_handles.service_handle, &cmd_uuid,
                                     ESP_GATT_PERM_WRITE,
                                     ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                                     &cmd_val, NULL);

                // Add Status Output
                esp_bt_uuid_t status_uuid;
                status_uuid.len = ESP_UUID_LEN_128;
                memcpy(status_uuid.uuid.uuid128, s_status_char_uuid, ESP_UUID_LEN_128);

                esp_attr_value_t status_val = { .attr_max_len = 32, .attr_len = 0, .attr_value = NULL };
                esp_ble_gatts_add_char(s_handles.service_handle, &status_uuid,
                                     ESP_GATT_PERM_READ,
                                     ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                     &status_val, NULL);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.len == ESP_UUID_LEN_128) {
                if (memcmp(param->add_char.char_uuid.uuid.uuid128, s_cmd_char_uuid, 16) == 0) {
                    s_handles.cmd_char_handle = param->add_char.attr_handle;
                } else if (memcmp(param->add_char.char_uuid.uuid.uuid128, s_status_char_uuid, 16) == 0) {
                    s_handles.status_char_handle = param->add_char.attr_handle;
                    // Add CCCD for Notify
                    esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
                    esp_attr_value_t cccd_val = { .attr_max_len = 2, .attr_len = 2, .attr_value = (uint8_t[]){0x00, 0x00} };
                    esp_ble_gatts_add_char_descr(s_handles.service_handle, &cccd_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_val, NULL);
                }
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == s_handles.cmd_char_handle) {
                handle_write_data(param->write.value, param->write.len);
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;

        default:
            break;
    }
}