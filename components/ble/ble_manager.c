#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "storage.h"
#include <string.h>

#include "ble_manager.h"
#include "ble_bipupu_service.h"
#include "ble_battery_service.h"
#include "ble_cts_service.h"

static const char* BLE_TAG = "ble_manager";

/* ================== 私有状态管理 ================== */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static ble_message_callback_t s_message_callback = NULL;
static ble_cts_time_callback_t s_cts_time_callback = NULL;
static uint16_t s_conn_id = 0xFFFF;
static uint32_t s_error_count = 0;
static esp_gatt_if_t s_gatts_if = 0;

// Service handles
static ble_bipupu_service_handles_t s_bipupu_handles = {0};
static ble_battery_service_handles_t s_battery_handles = {0};
static ble_cts_service_handles_t s_cts_handles = {0};

// UUID for advertising
static const uint8_t s_bipupu_service_uuid[16] = BIPUPU_SERVICE_UUID_128;

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

/* ================== BLE事件处理回调 ================== */
static void ble_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, 
                                     esp_ble_gatts_cb_param_t *param)
{
    esp_err_t ret;
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_ble_state = BLE_STATE_INITIALIZED;
                s_gatts_if = gatts_if;

                // Initialize services
                ret = ble_bipupu_service_init(gatts_if, &s_bipupu_handles);
                if (ret != ESP_OK) ble_handle_error("Init Bipupu Service", ret);

                ret = ble_battery_service_init(gatts_if, &s_battery_handles);
                if (ret != ESP_OK) ble_handle_error("Init Battery Service", ret);

                ret = ble_cts_service_init(gatts_if, &s_cts_handles);
                if (ret != ESP_OK) ble_handle_error("Init CTS Service", ret);

            } else {
                ble_handle_error("GATT Register", ESP_FAIL);
            }
            break;

        case ESP_GATTS_START_EVT:
            if (param->start.status == ESP_GATT_OK && param->start.service_handle == s_bipupu_handles.service_handle) {
                ble_manager_start_advertising();
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_ble_connected = true;
            s_conn_id = param->connect.conn_id;
            s_ble_state = BLE_STATE_CONNECTED;
            ESP_LOGI(BLE_TAG, "Device connected, conn_id=%d", s_conn_id);
            
            // Update connection params if needed
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x10;
            conn_params.max_int = 0x20;
            conn_params.latency = 0;
            conn_params.timeout = 400;
            esp_ble_gap_update_conn_params(&conn_params);
            
            ESP_LOGI(BLE_TAG, "CTS time service ready, waiting for time synchronization");
            
            char addr_str[32];
            snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            storage_save_ble_addr(addr_str);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_ble_connected = false;
            s_conn_id = 0xFFFF;
            s_ble_state = BLE_STATE_ADVERTISING;
            esp_ble_gap_start_advertising(NULL);
            break;

        default:
            // Delegate to service handlers
            ble_bipupu_service_handle_event(event, gatts_if, param);
            ble_battery_service_handle_event(event, gatts_if, param);
            ble_cts_service_handle_event(event, gatts_if, param);
            break;
    }
}

/* ================== BLE管理接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    if (s_ble_state == BLE_STATE_ERROR) {
        ESP_LOGW(BLE_TAG, "BLE处于错误状态，尝试重新初始化...");
        ble_manager_deinit();
        // 重置状态
        s_ble_state = BLE_STATE_UNINITIALIZED;
    }
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
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret != ESP_OK) {
        ble_handle_error("Config Advertising Data", ret);
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t ble_manager_deinit(void)
{
    // Deinit services
    ble_bipupu_service_deinit();
    ble_battery_service_deinit();
    ble_cts_service_deinit();
    
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
    ble_bipupu_service_set_message_callback(callback);
}

void ble_manager_set_cts_time_callback(ble_cts_time_callback_t callback)
{
    s_cts_time_callback = callback;
    ble_cts_service_set_time_callback(callback);
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
    // 可以在这里添加周期性任务，如电量低警告等
}

void ble_manager_update_battery_level(uint8_t level)
{
    ble_battery_service_update_level(level);
}