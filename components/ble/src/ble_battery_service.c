#include "ble_battery_service.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include <string.h>

static const char* BATTERY_TAG = "ble_battery_service";

static ble_battery_service_handles_t s_handles = {0};
static uint8_t s_battery_level = 100;
static esp_gatt_if_t s_gatts_if = 0;
static uint16_t s_conn_id = 0xFFFF;

/* UUIDs */
static const uint16_t s_battery_service_uuid = BATTERY_SERVICE_UUID;
static const uint16_t s_battery_level_uuid = BATTERY_LEVEL_UUID;

esp_err_t ble_battery_service_init(esp_gatt_if_t gatts_if, ble_battery_service_handles_t* handles) {
    if (!handles) return ESP_ERR_INVALID_ARG;

    s_gatts_if = gatts_if;

    esp_gatt_srvc_id_t service_id;
    service_id.is_primary = true;
    service_id.id.inst_id = 1;
    service_id.id.uuid.len = ESP_UUID_LEN_16;
    service_id.id.uuid.uuid.uuid16 = s_battery_service_uuid;

    esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(BATTERY_TAG, "Failed to create battery service: %s", esp_err_to_name(ret));
        return ret;
    }

    *handles = s_handles;
    return ESP_OK;
}

void ble_battery_service_deinit(void) {
    if (s_handles.service_handle != 0) {
        esp_ble_gatts_delete_service(s_handles.service_handle);
        s_handles.service_handle = 0;
        s_handles.level_char_handle = 0;
    }
}

void ble_battery_service_update_level(uint8_t level) {
    if (level > 100) level = 100;

    if (s_battery_level != level) {
        s_battery_level = level;
        ESP_LOGI(BATTERY_TAG, "Battery level updated: %d%%", s_battery_level);

        // 发送通知
        if (s_handles.level_char_handle != 0 && s_gatts_if != 0 && s_conn_id != 0xFFFF) {
            esp_err_t ret = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                                       s_handles.level_char_handle, 1, &s_battery_level, false);
            if (ret != ESP_OK) {
                ESP_LOGE(BATTERY_TAG, "Failed to send battery notification: %s", esp_err_to_name(ret));
            }
        }

        if (s_battery_level <= 20) {
            ESP_LOGW(BATTERY_TAG, "Low battery warning: %d%%", s_battery_level);
        }
    }
}

void ble_battery_service_handle_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_CREATE_EVT:
            if (param->create.service_id.id.inst_id == 1) {
                s_handles.service_handle = param->create.service_handle;
                esp_ble_gatts_start_service(s_handles.service_handle);

                // Add Battery Level
                esp_bt_uuid_t bat_uuid;
                bat_uuid.len = ESP_UUID_LEN_16;
                bat_uuid.uuid.uuid16 = s_battery_level_uuid;

                esp_attr_value_t bat_val = { .attr_max_len = 1, .attr_len = 1, .attr_value = &s_battery_level };
                esp_ble_gatts_add_char(s_handles.service_handle, &bat_uuid,
                                     ESP_GATT_PERM_READ,
                                     ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                     &bat_val, NULL);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.len == ESP_UUID_LEN_16 &&
                param->add_char.char_uuid.uuid.uuid16 == s_battery_level_uuid) {
                s_handles.level_char_handle = param->add_char.attr_handle;
                // Add CCCD for Notify
                esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
                esp_attr_value_t cccd_val = { .attr_max_len = 2, .attr_len = 2, .attr_value = (uint8_t[]){0x00, 0x00} };
                esp_ble_gatts_add_char_descr(s_handles.service_handle, &cccd_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_val, NULL);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_id = param->connect.conn_id;
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_conn_id = 0xFFFF;
            break;

        case ESP_GATTS_READ_EVT:
            if (param->read.handle == s_handles.level_char_handle) {
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
                rsp.attr_value.handle = param->read.handle;
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = s_battery_level;
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;

        default:
            break;
    }
}