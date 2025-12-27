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
static uint8_t s_service_handle = 0;
static uint8_t s_char_handle = 0;
static uint16_t s_conn_id = 0xFFFF;
static uint32_t s_error_count = 0;
static uint32_t s_last_error_time = 0;

/* ================== BLE UUID定义 ================== */
// 自定义服务UUID: 00001234-0000-0000-0000-000000000000
static const uint8_t s_service_uuid_128[16] = {
    0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 自定义特征UUID: 00005678-0000-0000-0000-000000000000
static const uint8_t s_char_uuid_128[16] = {
    0x78, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t s_char_value[1] = {0x00};

static esp_attr_value_t s_char_attr_value = {
    .attr_max_len = BLE_MAX_MESSAGE_LEN,
    .attr_len = sizeof(s_char_value),
    .attr_value = (uint8_t*)s_char_value,
};

/* ================== 错误处理 ================== */
static void ble_handle_error(const char* operation, esp_err_t error)
{
    s_error_count++;
    s_last_error_time = esp_log_timestamp();
    s_ble_state = BLE_STATE_ERROR;
    
    ESP_LOGE(BLE_TAG, "BLE错误 - 操作: %s, 错误码: %s (0x%x)",
             operation, esp_err_to_name(error), error);
}

/* ================== 状态字符串转换 ================== */
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
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(BLE_TAG, "GATT注册事件, app_id %d, status %d", 
                     param->reg.app_id, param->reg.status);
            if (param->reg.status == ESP_GATT_OK) {
                s_service_handle = gatts_if;
                s_ble_state = BLE_STATE_INITIALIZED;
                
                // 创建GATT服务
                esp_gatt_srvc_id_t service_id;
                service_id.is_primary = true;
                service_id.id.inst_id = 0;
                service_id.id.uuid.len = ESP_UUID_LEN_128;
                memcpy(service_id.id.uuid.uuid.uuid128, s_service_uuid_128, ESP_UUID_LEN_128);
                
                esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 4);
                if (ret != ESP_OK) {
                    ble_handle_error("创建GATT服务", ret);
                }
            } else {
                ble_handle_error("GATT注册", ESP_FAIL);
            }
            break;

        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(BLE_TAG, "GATT服务创建事件, status %d", param->create.status);
            if (param->create.status == ESP_GATT_OK) {
                s_service_handle = param->create.service_handle;
                s_ble_state = BLE_STATE_ADVERTISING;
                
                // 启动GATT服务
                esp_err_t ret = esp_ble_gatts_start_service(param->create.service_handle);
                if (ret != ESP_OK) {
                    ble_handle_error("启动GATT服务", ret);
                    break;
                }
                
                // 添加特征
                esp_bt_uuid_t char_uuid;
                char_uuid.len = ESP_UUID_LEN_128;
                memcpy(char_uuid.uuid.uuid128, s_char_uuid_128, ESP_UUID_LEN_128);
                
                ret = esp_ble_gatts_add_char(param->create.service_handle,
                                           &char_uuid,
                                           ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                           ESP_GATT_CHAR_PROP_BIT_READ | 
                                           ESP_GATT_CHAR_PROP_BIT_WRITE | 
                                           ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                           &s_char_attr_value, NULL);
                if (ret != ESP_OK) {
                    ble_handle_error("添加GATT特征", ret);
                }
            } else {
                ble_handle_error("创建GATT服务", ESP_FAIL);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(BLE_TAG, "GATT特征添加事件, status %d, attr_handle %d",
                    param->add_char.status, param->add_char.attr_handle);
            if (param->add_char.status == ESP_GATT_OK) {
                s_char_handle = param->add_char.attr_handle;
            } else {
                ble_handle_error("添加GATT特征", ESP_FAIL);
            }
            break;

        case ESP_GATTS_START_EVT:
            ESP_LOGI(BLE_TAG, "GATT服务启动事件, status %d", param->start.status);
            if (param->start.status == ESP_GATT_OK) {
                ESP_LOGI(BLE_TAG, "BLE服务已启动，准备开始广播...");
                // 自动启动广告
                esp_err_t ret = ble_manager_start_advertising();
                if (ret != ESP_OK) {
                    ble_handle_error("启动广告失败", ret);
                }
            } else {
                ble_handle_error("启动GATT服务", ESP_FAIL);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(BLE_TAG, "BLE连接事件, conn_id %d", param->connect.conn_id);
            s_ble_connected = true;
            s_conn_id = param->connect.conn_id;
            s_ble_state = BLE_STATE_CONNECTED;
            ESP_LOGI(BLE_TAG, "BLE已连接，设备名称: %s", BLE_DEVICE_NAME);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(BLE_TAG, "BLE断开连接事件, conn_id %d", param->disconnect.conn_id);
            s_ble_connected = false;
            s_conn_id = 0xFFFF;
            s_ble_state = BLE_STATE_ADVERTISING;
            ESP_LOGI(BLE_TAG, "BLE已断开，重新广播中...");
            break;

        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(BLE_TAG, "GATT写事件, conn_id %d, handle %d, len %d",
                    param->write.conn_id, param->write.handle, param->write.len);
            
            if (param->write.handle == s_char_handle && param->write.len > 0) {
                // 解析接收到的消息格式：sender|message
                char received_data[BLE_MAX_MESSAGE_LEN + 1] = {0};
                memcpy(received_data, param->write.value, 
                       (param->write.len < BLE_MAX_MESSAGE_LEN) ? param->write.len : BLE_MAX_MESSAGE_LEN);
                received_data[param->write.len] = '\0';
                
                ESP_LOGI(BLE_TAG, "接收到BLE消息: %s", received_data);
                
                // 解析消息格式
                char* separator = strchr(received_data, '|');
                if (separator != NULL) {
                    *separator = '\0';
                    char* sender = received_data;
                    char* message = separator + 1;
                    
                    // 调用消息回调函数
                    if (s_message_callback != NULL) {
                        s_message_callback(sender, message);
                    }
                }
            }
            break;

        default:
            break;
    }
}

/* ================== BLE管理接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    if (s_ble_state != BLE_STATE_UNINITIALIZED) {
        ESP_LOGW(BLE_TAG, "BLE已经初始化，当前状态: %s", 
                 ble_manager_state_to_string(s_ble_state));
        return ESP_OK;
    }
    
    ESP_LOGI(BLE_TAG, "初始化BLE管理器...");
    s_ble_state = BLE_STATE_INITIALIZING;
    
    esp_err_t ret = ESP_OK;
    
    // 1. 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器初始化", ret);
        return ret;
    }
    
    // 2. 使能蓝牙控制器
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器使能", ret);
        return ret;
    }
    
    // 3. 初始化蓝牙协议栈
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈初始化", ret);
        return ret;
    }
    
    // 4. 使能蓝牙协议栈
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈使能", ret);
        return ret;
    }
    
    // 5. 注册GATT回调函数
    ret = esp_ble_gatts_register_callback(ble_gatts_event_handler);
    if (ret != ESP_OK) {
        ble_handle_error("GATT回调注册", ret);
        return ret;
    }
    
    // 6. 注册GATT应用
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ble_handle_error("GATT应用注册", ret);
        return ret;
    }
    
    // 7. 设置设备名称
    ret = esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
    if (ret != ESP_OK) {
        ble_handle_error("设置设备名称", ret);
        return ret;
    }
    
    // 8. 配置广播数据
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
        .p_service_uuid = (uint8_t*)s_service_uuid_128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret != ESP_OK) {
        ble_handle_error("配置广播数据", ret);
        return ret;
    }
    
    ESP_LOGI(BLE_TAG, "BLE初始化完成");
    return ESP_OK;
}

esp_err_t ble_manager_deinit(void)
{
    ESP_LOGI(BLE_TAG, "反初始化BLE管理器...");
    
    esp_err_t ret = esp_ble_gap_stop_advertising();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ble_handle_error("停止广告", ret);
    }
    
    ret = esp_bluedroid_disable();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈禁用", ret);
    }
    
    ret = esp_bluedroid_deinit();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈反初始化", ret);
    }
    
    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器禁用", ret);
    }
    
    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器反初始化", ret);
        return ret;
    }
    
    s_ble_state = BLE_STATE_UNINITIALIZED;
    s_ble_connected = false;
    
    ESP_LOGI(BLE_TAG, "BLE反初始化完成");
    return ESP_OK;
}

esp_err_t ble_manager_start_advertising(void)
{
    if (s_ble_state == BLE_STATE_INITIALIZED || s_ble_state == BLE_STATE_ADVERTISING) {
        ESP_LOGI(BLE_TAG, "开始BLE广播...");
        
        esp_ble_adv_params_t adv_params = {
            .adv_int_min = BLE_ADV_INTERVAL_MIN,
            .adv_int_max = BLE_ADV_INTERVAL_MAX,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        
        esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
        if (ret != ESP_OK) {
            ble_handle_error("开始广播", ret);
            return ret;
        }
        
        s_ble_state = BLE_STATE_ADVERTISING;
        ESP_LOGI(BLE_TAG, "BLE广播已启动");
        return ESP_OK;
    } else {
        ESP_LOGW(BLE_TAG, "当前状态无法开始广播: %s", 
                 ble_manager_state_to_string(s_ble_state));
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t ble_manager_stop_advertising(void)
{
    if (s_ble_state == BLE_STATE_ADVERTISING) {
        ESP_LOGI(BLE_TAG, "停止BLE广播...");
        
        esp_err_t ret = esp_ble_gap_stop_advertising();
        if (ret != ESP_OK) {
            ble_handle_error("停止广播", ret);
            return ret;
        }
        
        s_ble_state = BLE_STATE_INITIALIZED;
        ESP_LOGI(BLE_TAG, "BLE广播已停止");
        return ESP_OK;
    }
    return ESP_OK;
}

void ble_manager_set_message_callback(ble_message_callback_t callback)
{
    s_message_callback = callback;
    if (callback == NULL) {
        ESP_LOGI(BLE_TAG, "消息回调已取消");
    } else {
        ESP_LOGI(BLE_TAG, "消息回调已设置");
    }
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
    // BLE事件通过回调函数处理，这里预留用于未来扩展
    // 可以在这里添加状态检查或心跳功能
}
