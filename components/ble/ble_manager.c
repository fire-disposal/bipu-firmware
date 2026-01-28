#include "ble_manager.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "nvs_flash.h"
#include <string.h>

static const char* BLE_TAG = "ble_manager";

/* ================== 私有状态管理 ================== */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static ble_message_callback_t s_message_callback = NULL;
static uint16_t s_conn_id = 0xFFFF;
static uint32_t s_error_count = 0;

// Handles
static uint16_t s_bipupu_service_handle;
static uint16_t s_cmd_char_handle;
static uint16_t s_status_char_handle;

static uint16_t s_battery_service_handle;
static uint16_t s_battery_level_char_handle;

// UUIDs
static const uint8_t s_bipupu_service_uuid[16] = BIPUPU_SERVICE_UUID_128;
static const uint8_t s_cmd_char_uuid[16] = BIPUPU_CHAR_CMD_UUID_128;
static const uint8_t s_status_char_uuid[16] = BIPUPU_CHAR_STATUS_UUID_128;
static const uint16_t s_battery_service_uuid = BATTERY_SERVICE_UUID;
static const uint16_t s_battery_level_uuid = BATTERY_LEVEL_UUID;

// Battery Level
static uint8_t s_battery_level = 100;

/* ================== 辅助函数 ================== */
static void ble_handle_error(const char* operation, esp_err_t error)
{
    s_error_count++;
    s_ble_state = BLE_STATE_ERROR;
    ESP_LOGE(BLE_TAG, "BLE错误 - 操作: %s, 错误码: %s (0x%x)",
             operation, esp_err_to_name(error), error);
}

const char* ble_manager_state_to_string(ble_state_t state)
{
    switch (state) {
        case BLE_STATE_UNINITIALIZED: return "未初始化";
        case BLE_STATE_INITIALIZING:  return "初始化中";
        case BLE_STATE_INITIALIZED:   return "已初始化";
        case BLE_STATE_ADVERTISING:   return "广播中";
        case BLE_STATE_CONNECTED:     return "已连接";
        case BLE_STATE_ERROR:         return "错误状态";
        default: return "未知状态";
    }
}

/* ================== 协议处理 ================== */
static void handle_write_data(const uint8_t *data, uint16_t len) {
    ble_parsed_msg_t msg;
    if (ble_protocol_parse(data, len, &msg)) {
        if (s_message_callback) {
            s_message_callback(msg.sender, msg.message, &msg.effect);
        }
    }
}

/* ================== BLE事件处理回调 ================== */
static void ble_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, 
                                     esp_ble_gatts_cb_param_t *param)
{
    esp_err_t ret;
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_ble_state = BLE_STATE_INITIALIZED;
                
                // 1. Create Bipupu Service
                esp_gatt_srvc_id_t bipupu_service_id;
                bipupu_service_id.is_primary = true;
                bipupu_service_id.id.inst_id = 0;
                bipupu_service_id.id.uuid.len = ESP_UUID_LEN_128;
                memcpy(bipupu_service_id.id.uuid.uuid.uuid128, s_bipupu_service_uuid, ESP_UUID_LEN_128);
                
                ret = esp_ble_gatts_create_service(gatts_if, &bipupu_service_id, 6); // Handles: Svc + Cmd + Status + 2*Desc
                if (ret != ESP_OK) ble_handle_error("Create Bipupu Service", ret);

                // 2. Create Battery Service
                esp_gatt_srvc_id_t battery_service_id;
                battery_service_id.is_primary = true;
                battery_service_id.id.inst_id = 1;
                battery_service_id.id.uuid.len = ESP_UUID_LEN_16;
                battery_service_id.id.uuid.uuid.uuid16 = s_battery_service_uuid;

                ret = esp_ble_gatts_create_service(gatts_if, &battery_service_id, 4);
                if (ret != ESP_OK) ble_handle_error("Create Battery Service", ret);

            } else {
                ble_handle_error("GATT Register", ESP_FAIL);
            }
            break;

        case ESP_GATTS_CREATE_EVT:
            if (param->create.status == ESP_GATT_OK) {
                if (param->create.service_id.id.inst_id == 0) { // Bipupu Service
                    s_bipupu_service_handle = param->create.service_handle;
                    esp_ble_gatts_start_service(s_bipupu_service_handle);

                    // Add Command Input
                    esp_bt_uuid_t cmd_uuid;
                    cmd_uuid.len = ESP_UUID_LEN_128;
                    memcpy(cmd_uuid.uuid.uuid128, s_cmd_char_uuid, ESP_UUID_LEN_128);
                    
                    esp_attr_value_t cmd_val = { .attr_max_len = BLE_MAX_MESSAGE_LEN, .attr_len = 0, .attr_value = NULL };
                    esp_ble_gatts_add_char(s_bipupu_service_handle, &cmd_uuid,
                                         ESP_GATT_PERM_WRITE,
                                         ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                                         &cmd_val, NULL);

                    // Add Status Output
                    esp_bt_uuid_t status_uuid;
                    status_uuid.len = ESP_UUID_LEN_128;
                    memcpy(status_uuid.uuid.uuid128, s_status_char_uuid, ESP_UUID_LEN_128);
                    
                    esp_attr_value_t status_val = { .attr_max_len = 32, .attr_len = 0, .attr_value = NULL };
                    esp_ble_gatts_add_char(s_bipupu_service_handle, &status_uuid,
                                         ESP_GATT_PERM_READ,
                                         ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                         &status_val, NULL);

                } else if (param->create.service_id.id.inst_id == 1) { // Battery Service
                    s_battery_service_handle = param->create.service_handle;
                    esp_ble_gatts_start_service(s_battery_service_handle);

                    // Add Battery Level
                    esp_bt_uuid_t bat_uuid;
                    bat_uuid.len = ESP_UUID_LEN_16;
                    bat_uuid.uuid.uuid16 = s_battery_level_uuid;

                    esp_attr_value_t bat_val = { .attr_max_len = 1, .attr_len = 1, .attr_value = &s_battery_level };
                    esp_ble_gatts_add_char(s_battery_service_handle, &bat_uuid,
                                         ESP_GATT_PERM_READ,
                                         ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                         &bat_val, NULL);
                }
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.status == ESP_GATT_OK) {
                if (param->add_char.char_uuid.len == ESP_UUID_LEN_128) {
                    if (memcmp(param->add_char.char_uuid.uuid.uuid128, s_cmd_char_uuid, 16) == 0) {
                        s_cmd_char_handle = param->add_char.attr_handle;
                    } else if (memcmp(param->add_char.char_uuid.uuid.uuid128, s_status_char_uuid, 16) == 0) {
                        s_status_char_handle = param->add_char.attr_handle;
                        // Add CCCD for Notify
                        esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
                        esp_attr_value_t cccd_val = { .attr_max_len = 2, .attr_len = 2, .attr_value = (uint8_t[]){0x00, 0x00} };
                        esp_ble_gatts_add_char_descr(s_bipupu_service_handle, &cccd_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_val, NULL);
                    }
                } else if (param->add_char.char_uuid.len == ESP_UUID_LEN_16) {
                    if (param->add_char.char_uuid.uuid.uuid16 == s_battery_level_uuid) {
                        s_battery_level_char_handle = param->add_char.attr_handle;
                        // Add CCCD for Notify
                        esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG } };
                        esp_attr_value_t cccd_val = { .attr_max_len = 2, .attr_len = 2, .attr_value = (uint8_t[]){0x00, 0x00} };
                        esp_ble_gatts_add_char_descr(s_battery_service_handle, &cccd_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_val, NULL);
                    }
                }
            }
            break;

        case ESP_GATTS_START_EVT:
            if (param->start.status == ESP_GATT_OK && param->start.service_handle == s_bipupu_service_handle) {
                ble_manager_start_advertising();
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_ble_connected = true;
            s_conn_id = param->connect.conn_id;
            s_ble_state = BLE_STATE_CONNECTED;
            // Update connection params if needed
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x10; // 20ms
            conn_params.max_int = 0x20; // 40ms
            conn_params.latency = 0;
            conn_params.timeout = 400;
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_ble_connected = false;
            s_conn_id = 0xFFFF;
            s_ble_state = BLE_STATE_ADVERTISING;
            esp_ble_gap_start_advertising(NULL); // Restart advertising
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == s_cmd_char_handle) {
                handle_write_data(param->write.value, param->write.len);
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            } else {
                // Handle CCCD writes
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;
            
        case ESP_GATTS_READ_EVT:
            if (param->read.handle == s_battery_level_char_handle) {
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

/* ================== BLE管理接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    if (s_ble_state != BLE_STATE_UNINITIALIZED) return ESP_OK;
    
    s_ble_state = BLE_STATE_INITIALIZING;
    esp_err_t ret;
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) return ret;
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) return ret;
    
    ret = esp_bluedroid_init();
    if (ret) return ret;
    
    ret = esp_bluedroid_enable();
    if (ret) return ret;
    
    ret = esp_ble_gatts_register_callback(ble_gatts_event_handler);
    if (ret) return ret;
    
    ret = esp_ble_gatts_app_register(0);
    if (ret) return ret;
    
    ret = esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
    if (ret) return ret;
    
    // Config Advertising Data
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = BLE_ADV_INTERVAL_MIN,
        .max_interval = BLE_ADV_INTERVAL_MAX,
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = ESP_UUID_LEN_128,
        .p_service_uuid = (uint8_t*)s_bipupu_service_uuid,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    esp_ble_gap_config_adv_data(&adv_data);
    
    return ESP_OK;
}

esp_err_t ble_manager_deinit(void)
{
    // Simplified deinit
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    s_ble_state = BLE_STATE_UNINITIALIZED;
    return ESP_OK;
}

esp_err_t ble_manager_start_advertising(void)
{
    esp_ble_adv_params_t adv_params = {
        .adv_int_min = BLE_ADV_INTERVAL_MIN,
        .adv_int_max = BLE_ADV_INTERVAL_MAX,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    return esp_ble_gap_start_advertising(&adv_params);
}

esp_err_t ble_manager_stop_advertising(void)
{
    return esp_ble_gap_stop_advertising();
}

void ble_manager_set_message_callback(ble_message_callback_t callback)
{
    s_message_callback = callback;
}

bool ble_manager_is_connected(void)
{
    return s_ble_connected;
}

ble_state_t ble_manager_get_state(void)
{
    return s_ble_state;
}

const char* ble_manager_get_device_name(void)
{
    return BLE_DEVICE_NAME;
}

uint32_t ble_manager_get_error_count(void)
{
    return s_error_count;
}

void ble_manager_poll(void)
{
}

void ble_manager_update_battery_level(uint8_t level)
{
    if (level > 100) level = 100;
    s_battery_level = level;
    
    if (s_ble_connected && s_battery_level_char_handle != 0) {
        esp_ble_gatts_send_indicate(s_bipupu_service_handle, s_conn_id, s_battery_level_char_handle, 1, &s_battery_level, false);
    }
}
