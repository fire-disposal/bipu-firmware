/**
 * @file ble_manager.c
 * @brief BLE 管理器实现 (Bluedroid版本)
 *
 * 基于 ESP-IDF Bluedroid 实现的 Bipupu 蓝牙协议
 * 协议格式：[协议头 (0xB0)][时间戳 (4)][消息类型 (1)][数据长度 (2)][数据 (N)][校验和 (1)]
 *
 * 自包含的 BLE 消息处理：
 *   - 在蓝牙任务中接收数据 → 入队（非阻塞）
 *   - 在 app_task 中 ble_manager_process_pending_messages() → 出队并调用回调
 * 
 * 安全启动机制：
 *   - 状态机驱动的初始化流程
 *   - 错误重试和恢复机制
 *   - 资源自动释放和重建
 */

#include "ble_manager.h"
#include "bipupu_protocol.h"
#include "board.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <time.h>


/* ================== 绑定相关常量 ================== */
#define BINDING_NVS_NAMESPACE "ble_binding"
#define BINDING_NVS_KEY "bound_device"
#define BINDING_NVS_NAME_KEY "bound_name"


/* ================== 消息队列配置 ================== */

/** 单条消息事件结构体 */
typedef struct {
    char     sender[32];   /**< 发送者名称 */
    char     body[128];    /**< 消息正文 */
    uint32_t timestamp;    /**< Unix 时间戳 */
} ble_msg_event_t;

/** 队列深度：缓冲最多 8 条消息（内存开销约 1.3 KB） */
#define MSG_QUEUE_DEPTH  8

static QueueHandle_t s_msg_queue = NULL;


/* ================== 安全启动相关常量 ================== */
#define BLE_INIT_MAX_RETRIES        3       /**< 初始化最大重试次数 */
#define BLE_INIT_RETRY_DELAY_MS     500     /**< 重试间隔 */
#define BLE_TASK_STACK_SIZE         4096    /**< 蓝牙任务堆栈大小 */
#define BLE_TASK_PRIORITY           5       /**< 蓝牙任务优先级 */
#define BLE_EVENT_TIMEOUT_MS        10000   /**< BLE事件超时 */


/* ================== Nordic UART Service UUIDs ================== */
/* NUS 服务 UUID: 6e400001-b5a3-f393-e0a9-e50e24dcca9e */
static const uint8_t nus_service_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
};

/* NUS RX 特征值 UUID (手机→设备): 6e400002-b5a3-f393-e0a9-e50e24dcca9e */
static const uint8_t nus_rx_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
};

/* NUS TX 特征值 UUID (设备→手机): 6e400003-b5a3-f393-e0a9-e50e24dcca9e */
static const uint8_t nus_tx_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
};


/* ================== 广告配置 ================== */
#define DEVICE_NAME_PREFIX          "Bipupu_"
#define ADV_INTERVAL_MIN_MS         500
#define ADV_INTERVAL_MAX_MS         1000
#define ADV_DURATION_SEC            0


/* ================== 全局状态 ================== */
static const char *TAG = "BLE";
static char s_bound_device_addr[18] = {0};
static char s_bound_device_name[32] = {0};
static bool s_is_bound = false;

/* 当前连接的对端地址 */
static esp_bd_addr_t s_current_remote_addr = {0};
static bool s_current_addr_valid = false;


/* ================== BLE 状态 ================== */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static uint16_t s_conn_id = 0xFFFF;
static uint16_t s_gatts_if = 0;
static uint32_t s_error_count = 0;
static uint8_t s_init_retry_count = 0;

/* 全局连接状态标识 */
bool ble_is_connected = false;

static ble_message_callback_t s_message_callback = NULL;
static ble_time_sync_callback_t s_time_sync_callback = NULL;
static ble_connection_callback_t s_connection_callback = NULL;


/* ================== GATT 服务句柄 ================== */
static uint16_t s_service_handle = 0;
static uint16_t s_rx_char_handle = 0;
static uint16_t s_tx_char_handle = 0;
static uint16_t s_tx_ccc_handle = 0;

/* 设备名称 */
static char s_device_name[32] = {0};


/* ================== 属性表索引 ================== */
enum {
    IDX_SVC,
    IDX_RX_CHAR,
    IDX_RX_VAL,
    IDX_TX_CHAR,
    IDX_TX_VAL,
    IDX_TX_CCC,
    HRS_IDX_NB,
};


/* ================== 绑定相关函数声明 ================== */
static void handle_binding_packet(const uint8_t* data, size_t length);
static void save_binding_info(void);
static void clear_binding_info(void);
static void load_binding_info(void);
static bool check_binding_match(void);


/* ================== 私有函数声明 ================== */
static void handle_time_sync_directly(uint32_t timestamp);
static void handle_received_packet(const uint8_t* data, size_t length);
static void generate_device_name(void);
static void update_ble_state(ble_state_t new_state);
static esp_err_t ble_stack_init(void);
static esp_err_t ble_stack_deinit(void);
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static esp_err_t nus_tx_notify(const uint8_t* data, size_t length);
static void send_ack_response(uint32_t original_message_id);
static esp_err_t start_advertising(void);


/* ================== GATT 属性表 ================== */
/* 定义UUID */
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static uint8_t rx_value[512] = {0};
static uint8_t tx_value[512] = {0};

/* 客户端特征配置描述符默认值 (通知启用) */
static uint8_t ccc_value[2] = {0x00, 0x00};

/* 完整的GATT属性表 */
static esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP}, {16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                 sizeof(uint16_t), 16, (uint8_t *)&nus_service_uuid}},

    // RX Characteristic Declaration
    [IDX_RX_CHAR] = {{ESP_GATT_AUTO_RSP}, {2, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                     1, 1, (uint8_t *)&char_prop_read_write}},

    // RX Characteristic Value
    [IDX_RX_VAL] = {{ESP_GATT_AUTO_RSP}, {16, (uint8_t *)&nus_rx_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    512, 512, rx_value}},

    // TX Characteristic Declaration
    [IDX_TX_CHAR] = {{ESP_GATT_AUTO_RSP}, {2, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                     1, 1, (uint8_t *)&char_prop_read_notify}},

    // TX Characteristic Value
    [IDX_TX_VAL] = {{ESP_GATT_AUTO_RSP}, {16, (uint8_t *)&nus_tx_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    512, 512, tx_value}},

    // TX Client Characteristic Configuration Descriptor
    [IDX_TX_CCC] = {{ESP_GATT_AUTO_RSP}, {2, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    2, 2, ccc_value}},
};


/* ================== 辅助函数 ================== */

static void generate_device_name(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_device_name, sizeof(s_device_name),
             "%s%02X%02X%02X", DEVICE_NAME_PREFIX, mac[3], mac[4], mac[5]);
}

static void update_ble_state(ble_state_t new_state)
{
    if (s_ble_state != new_state) {
        ESP_LOGI(TAG, "状态变化: %d -> %d", s_ble_state, new_state);
        s_ble_state = new_state;
    }
}


/* ================== 广告数据构建 ================== */

static uint8_t s_adv_data[31] = {0};
static uint8_t s_adv_data_len = 0;

static esp_err_t build_adv_data(void)
{
    uint8_t *p = s_adv_data;
    uint8_t len = 0;

    // Flags
    *p++ = 2;                   // Length
    *p++ = ESP_BLE_AD_TYPE_FLAG;
    *p++ = 0x06;                // LE General Discoverable + BR/EDR Not Supported
    len += 3;

    // TX Power Level
    *p++ = 2;
    *p++ = ESP_BLE_AD_TYPE_TX_PWR;
    *p++ = 0x00;
    len += 3;

    // Complete Local Name
    uint8_t name_len = strlen(s_device_name);
    if (name_len > 0 && len + name_len + 2 <= 31) {
        *p++ = name_len + 1;
        *p++ = ESP_BLE_AD_TYPE_NAME_CMPL;
        memcpy(p, s_device_name, name_len);
        p += name_len;
        len += name_len + 2;
    }

    // 128-bit Service UUID
    if (len + 18 <= 31) {
        *p++ = 17;
        *p++ = ESP_BLE_AD_TYPE_128SRV_CMPL;
        memcpy(p, nus_service_uuid, 16);
        p += 16;
        len += 18;
    }

    s_adv_data_len = len;
    
    // 广告数据已在 s_adv_data 缓冲区中准备好
    // 实际的广告配置将在 GAP 事件处理中进行
    return ESP_OK;
}


/* ================== GAP 事件处理 ================== */

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "广告数据设置完成");
            if (s_ble_state == BLE_STATE_IDLE) {
                start_advertising();
            }
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "广告开始成功");
                update_ble_state(BLE_STATE_ADVERTISING);
            } else {
                ESP_LOGE(TAG, "广告开始失败: %d", param->adv_start_cmpl.status);
                update_ble_state(BLE_STATE_ERROR);
                s_error_count++;
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "广告停止成功");
                update_ble_state(BLE_STATE_IDLE);
            } else {
                ESP_LOGE(TAG, "广告停止失败: %d", param->adv_stop_cmpl.status);
            }
            break;

        default:
            break;
    }
}


/* ================== GATT 事件处理 ================== */

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, 
                                        esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATT注册成功, app_id=%d", param->reg.app_id);
            s_gatts_if = gatts_if;

            // 设置设备名称
            esp_ble_gap_set_device_name(s_device_name);

            // 创建属性表
            esp_err_t ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "创建属性表失败: %s", esp_err_to_name(ret));
                update_ble_state(BLE_STATE_ERROR);
            }
            break;
        }

        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "属性表创建成功，num_handle=%d", param->add_attr_tab.num_handle);
                s_service_handle = param->add_attr_tab.handles[IDX_SVC];
                s_rx_char_handle = param->add_attr_tab.handles[IDX_RX_VAL];
                s_tx_char_handle = param->add_attr_tab.handles[IDX_TX_VAL];
                s_tx_ccc_handle = param->add_attr_tab.handles[IDX_TX_CCC];
                
                ESP_LOGI(TAG, "Service handle=%d, RX handle=%d, TX handle=%d, CCC handle=%d", 
                        s_service_handle, s_rx_char_handle, s_tx_char_handle, s_tx_ccc_handle);
                
                // 启动服务
                esp_ble_gatts_start_service(s_service_handle);
                update_ble_state(BLE_STATE_IDLE);
            } else {
                ESP_LOGE(TAG, "属性表创建失败：%d", param->add_attr_tab.status);
                update_ble_state(BLE_STATE_ERROR);
            }
            break;
        }

        case ESP_GATTS_CONNECT_EVT: {
            s_conn_id = param->connect.conn_id;
            s_ble_connected = true;
            ble_is_connected = true;
            update_ble_state(BLE_STATE_CONNECTED);

            // 保存对端地址
            memcpy(s_current_remote_addr, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            s_current_addr_valid = true;
            
            ESP_LOGI(TAG, "设备连接, conn_id=%d, addr=" ESP_BD_ADDR_STR, 
                    s_conn_id, ESP_BD_ADDR_HEX(s_current_remote_addr));

            if (s_connection_callback) {
                s_connection_callback(true);
            }

            // 更新连接参数
            esp_ble_conn_update_params_t conn_params = {
                .min_int = 0x10,    // 最小连接间隔
                .max_int = 0x20,    // 最大连接间隔
                .latency = 0,       // 延迟
                .timeout = 400      // 超时
            };
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT: {
            ESP_LOGI(TAG, "设备断开连接, reason=%d", param->disconnect.reason);
            s_ble_connected = false;
            ble_is_connected = false;
            s_conn_id = 0xFFFF;
            s_current_addr_valid = false;
            memset(s_current_remote_addr, 0, sizeof(s_current_remote_addr));
            update_ble_state(BLE_STATE_IDLE);

            if (s_connection_callback) {
                s_connection_callback(false);
            }

            // 重新开始广告
            start_advertising();
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            if (!param->write.is_prep) {
                ESP_LOGI(TAG, "收到写入, handle=%d, len=%d", param->write.handle, param->write.len);
                
                // 检查是否是RX特征值
                if (param->write.handle == s_rx_char_handle && param->write.len > 0) {
                    handle_received_packet(param->write.value, param->write.len);
                }

                // 发送写入响应
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, 
                                                param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;
        }

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU更新: %d", param->mtu.mtu);
            break;

        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, 
                                esp_ble_gatts_cb_param_t *param)
{
    // 应用注册事件
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "应用注册成功, app_id=%d", param->reg.app_id);
        } else {
            ESP_LOGE(TAG, "应用注册失败, app_id=%d, status=%d", param->reg.app_id, param->reg.status);
            return;
        }
    }

    // 如果gatts_if不为有效值，跳过
    if (gatts_if == ESP_GATT_IF_NONE) {
        return;
    }

    // 处理profile事件
    gatts_profile_event_handler(event, gatts_if, param);
}


/* ================== 蓝牙协议栈初始化 ================== */

static esp_err_t ble_stack_init(void)
{
    esp_err_t ret;

    // 1. 初始化 NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS需要擦除后重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙控制器初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 启用蓝牙控制器 (BLE模式)
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙控制器启用失败: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return ret;
    }

    // 4. 初始化 Bluedroid 协议栈
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid初始化失败: %s", esp_err_to_name(ret));
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    // 5. 启用 Bluedroid
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid启用失败: %s", esp_err_to_name(ret));
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    // 6. 注册 GAP 回调
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP回调注册失败: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    // 7. 注册 GATT Server 回调
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT回调注册失败: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    // 8. 注册应用
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT应用注册失败: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    // 9. 设置MTU
    esp_ble_gatt_set_local_mtu(517);

    ESP_LOGI(TAG, "蓝牙协议栈初始化成功");
    return ESP_OK;
}

static esp_err_t ble_stack_deinit(void)
{
    // 停止广告 - 是一个空操作，正常通过事件处理

    // 断开连接
    if (s_ble_connected && s_conn_id != 0xFFFF) {
        esp_ble_gatts_close(s_gatts_if, s_conn_id);
    }

    // 停止服务
    if (s_service_handle != 0) {
        esp_ble_gatts_stop_service(s_service_handle);
    }

    // 禁用和反初始化 Bluedroid
    esp_bluedroid_disable();
    esp_bluedroid_deinit();

    // 禁用和反初始化控制器
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    s_ble_state = BLE_STATE_UNINITIALIZED;
    s_ble_connected = false;
    ble_is_connected = false;
    s_conn_id = 0xFFFF;
    s_service_handle = 0;

    ESP_LOGI(TAG, "蓝牙协议栈反初始化完成");
    return ESP_OK;
}


/* ================== 广告控制 ================== */

static esp_err_t start_advertising(void)
{
    if (s_ble_state == BLE_STATE_ADVERTISING) {
        return ESP_OK;
    }

    // 广告数据已在 build_adv_data 中准备好
    // 仅返回成功状态
    update_ble_state(BLE_STATE_ADVERTISING);
    return ESP_OK;
}


/* ================== 数据包处理 ================== */

static void handle_received_packet(const uint8_t* data, size_t length)
{
    bipupu_parsed_packet_t packet;

    if (!bipupu_protocol_parse(data, length, &packet)) {
        ESP_LOGW(TAG, "解析失败");
        s_error_count++;
        return;
    }

    /* 绑定安全校验 */
    if (s_is_bound) {
        bipupu_message_type_t mt = packet.message_type;
        bool is_bind_cmd = (mt == BIPUPU_MSG_BINDING_INFO ||
                            mt == BIPUPU_MSG_UNBIND_COMMAND);
        if (!is_bind_cmd && !check_binding_match()) {
            ESP_LOGW(TAG, "拒绝非绑定设备 type=0x%02X", mt);
            s_error_count++;
            return;
        }
    }

    switch (packet.message_type) {
        case BIPUPU_MSG_TIME_SYNC:
            handle_time_sync_directly(packet.timestamp);
            if (s_time_sync_callback) {
                s_time_sync_callback(packet.timestamp);
            }
            break;

        case BIPUPU_MSG_TEXT:
            if (s_msg_queue != NULL) {
                ble_msg_event_t evt;
                strncpy(evt.sender, packet.sender_name, sizeof(evt.sender) - 1);
                evt.sender[sizeof(evt.sender) - 1] = '\0';
                strncpy(evt.body, packet.body_text, sizeof(evt.body) - 1);
                evt.body[sizeof(evt.body) - 1] = '\0';
                evt.timestamp = packet.timestamp;

                if (xQueueSend(s_msg_queue, &evt, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "队列已满丢弃 [%s]", packet.sender_name);
                } else {
                    // 消息成功入队，发送 ACK 确认
                    send_ack_response(packet.timestamp);
                }
            }
            break;

        case BIPUPU_MSG_ACKNOWLEDGEMENT:
            break;

        case BIPUPU_MSG_BINDING_INFO:
        case BIPUPU_MSG_UNBIND_COMMAND:
            handle_binding_packet(data, length);
            break;

        default:
            ESP_LOGW(TAG, "未知类型 0x%02X", packet.message_type);
            break;
    }
}


/* ================== 响应发送 ================== */

static esp_err_t nus_tx_notify(const uint8_t* data, size_t length)
{
    if (!s_ble_connected || s_conn_id == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || length == 0 || length > 512) {
        return ESP_ERR_INVALID_ARG;
    }

    // 发送通知
    esp_err_t ret = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_tx_char_handle, 
                                                 length, (uint8_t *)data, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送通知失败: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void send_ack_response(uint32_t original_message_id)
{
    if (!s_ble_connected || s_conn_id == 0xFFFF) {
        return;
    }

    uint8_t buffer[64];
    size_t packet_length = bipupu_protocol_create_acknowledgement(
        original_message_id, buffer, sizeof(buffer));

    if (packet_length > 0) {
        nus_tx_notify(buffer, packet_length);
        ESP_LOGI(TAG, "已发送 ACK: msg_id=%u", original_message_id);
    }
}


/* ================== 绑定管理 ================== */

static void handle_binding_packet(const uint8_t* data, size_t length)
{
    bipupu_parsed_packet_t parsed;
    if (!bipupu_protocol_parse(data, length, &parsed)) {
        ESP_LOGW(TAG, "解析绑定数据失败");
        return;
    }

    switch (parsed.message_type) {
        case BIPUPU_MSG_BINDING_INFO: {
            char json_str[256];
            bipupu_protocol_decode_utf8_safe(parsed.data, parsed.data_length, json_str);
            ESP_LOGI(TAG, "绑定 %s", json_str);
            save_binding_info();
            // 发送 ACK 确认绑定成功
            send_ack_response(parsed.timestamp);
            break;
        }

        case BIPUPU_MSG_UNBIND_COMMAND:
            ESP_LOGI(TAG, "解绑");
            clear_binding_info();
            // 发送解绑确认响应
            send_ack_response(parsed.timestamp);
            // 延迟断开连接，确保响应发送完成
            vTaskDelay(pdMS_TO_TICKS(50));
            if (s_ble_connected && s_conn_id != 0xFFFF) {
                esp_ble_gatts_close(s_gatts_if, s_conn_id);
            }
            break;

        default:
            break;
    }
}

static void save_binding_info(void)
{
    if (!s_ble_connected || s_conn_id == 0xFFFF || !s_current_addr_valid) {
        ESP_LOGW(TAG, "无法保存绑定信息: 未连接或无有效地址");
        return;
    }
    
    // 转换地址为字符串格式
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             s_current_remote_addr[0], s_current_remote_addr[1], s_current_remote_addr[2],
             s_current_remote_addr[3], s_current_remote_addr[4], s_current_remote_addr[5]);
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 失败 %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(nvs_handle, BINDING_NVS_KEY, addr_str);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "保存地址失败");
        nvs_close(nvs_handle);
        return;
    }

    if (strlen(s_bound_device_name) > 0) {
        nvs_set_str(nvs_handle, BINDING_NVS_NAME_KEY, s_bound_device_name);
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    strncpy(s_bound_device_addr, addr_str, sizeof(s_bound_device_addr) - 1);
    s_is_bound = true;
    ESP_LOGI(TAG, "绑定成功 %s", addr_str);
}

static void clear_binding_info(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    nvs_erase_key(nvs_handle, BINDING_NVS_KEY);
    nvs_erase_key(nvs_handle, BINDING_NVS_NAME_KEY);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    memset(s_bound_device_addr, 0, sizeof(s_bound_device_addr));
    memset(s_bound_device_name, 0, sizeof(s_bound_device_name));
    s_is_bound = false;
    ESP_LOGI(TAG, "解绑成功");
}

static void load_binding_info(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, BINDING_NVS_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        err = nvs_get_str(nvs_handle, BINDING_NVS_KEY, s_bound_device_addr, &required_size);
        if (err == ESP_OK) {
            s_is_bound = true;
        }
    }

    required_size = 0;
    err = nvs_get_str(nvs_handle, BINDING_NVS_NAME_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        nvs_get_str(nvs_handle, BINDING_NVS_NAME_KEY, s_bound_device_name, &required_size);
    }

    nvs_close(nvs_handle);
}

static bool check_binding_match(void)
{
    if (!s_is_bound || !ble_is_connected || !s_current_addr_valid) {
        return false;
    }
    
    // 将当前地址转换为字符串
    char current_addr[18];
    snprintf(current_addr, sizeof(current_addr), "%02x:%02x:%02x:%02x:%02x:%02x",
             s_current_remote_addr[0], s_current_remote_addr[1], s_current_remote_addr[2],
             s_current_remote_addr[3], s_current_remote_addr[4], s_current_remote_addr[5]);
    
    return (strcmp(current_addr, s_bound_device_addr) == 0);
}


/* ================== 时间同步处理 ================== */

static void handle_time_sync_directly(uint32_t timestamp)
{
    time_t time_val = (time_t)timestamp;
    struct tm *timeinfo = localtime(&time_val);

    if (!timeinfo) {
        return;
    }

    esp_err_t ret = board_set_rtc(
        timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    );

    if (ret == ESP_OK) {
        board_notify();
    }
}


/* ================== 公共接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    if (s_ble_state != BLE_STATE_UNINITIALIZED) {
        ESP_LOGW(TAG, "BLE已初始化，当前状态: %d", s_ble_state);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "开始初始化BLE (Bluedroid)");

    // 生成设备名称
    generate_device_name();
    ESP_LOGI(TAG, "设备名称: %s", s_device_name);

    // 加载绑定信息
    load_binding_info();

    // 带重试的安全初始化
    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < BLE_INIT_MAX_RETRIES; i++) {
        ret = ble_stack_init();
        if (ret == ESP_OK) {
            break;
        }
        
        s_init_retry_count++;
        ESP_LOGW(TAG, "初始化尝试 %d/%d 失败, 错误: %s", i + 1, BLE_INIT_MAX_RETRIES, esp_err_to_name(ret));
        
        if (i < BLE_INIT_MAX_RETRIES - 1) {
            ESP_LOGI(TAG, "等待 %d ms后重试...", BLE_INIT_RETRY_DELAY_MS * (i + 1));
            vTaskDelay(pdMS_TO_TICKS(BLE_INIT_RETRY_DELAY_MS * (i + 1)));
            ble_stack_deinit();
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE初始化失败，重试 %d 次后放弃", BLE_INIT_MAX_RETRIES);
        update_ble_state(BLE_STATE_ERROR);
        return ret;
    }

    ESP_LOGI(TAG, "BLE初始化成功 (重试次数: %d)", s_init_retry_count);
    return ESP_OK;
}

esp_err_t ble_manager_message_queue_init(void)
{
    if (s_msg_queue != NULL) {
        return ESP_OK;
    }

    s_msg_queue = xQueueCreate(MSG_QUEUE_DEPTH, sizeof(ble_msg_event_t));
    if (s_msg_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void ble_manager_process_pending_messages(void)
{
    if (s_msg_queue == NULL) {
        return;
    }

    ble_msg_event_t evt;
    while (xQueueReceive(s_msg_queue, &evt, 0) == pdTRUE) {
        if (s_message_callback) {
            s_message_callback(evt.sender, evt.body, evt.timestamp);
        }
    }
}

esp_err_t ble_manager_deinit(void)
{
    if (s_ble_state == BLE_STATE_UNINITIALIZED) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "BLE反初始化");
    return ble_stack_deinit();
}

esp_err_t ble_manager_start_advertising(void)
{
    if (s_ble_state != BLE_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_connected) {
        return ESP_OK;
    }

    return start_advertising();
}

esp_err_t ble_manager_stop_advertising(void)
{
    if (s_ble_state != BLE_STATE_ADVERTISING) {
        return ESP_OK;
    }

    // Bluedroid 中没有 stop_advertising，需要重新配置参数
    // 实际上停止广告需要通过事件处理
    return ESP_OK;
}

void ble_manager_set_message_callback(ble_message_callback_t callback)
{
    s_message_callback = callback;
}

void ble_manager_set_time_sync_callback(ble_time_sync_callback_t callback)
{
    s_time_sync_callback = callback;
}

void ble_manager_set_connection_callback(ble_connection_callback_t callback)
{
    s_connection_callback = callback;
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
    return s_device_name;
}

uint32_t ble_manager_get_error_count(void)
{
    return s_error_count;
}

void ble_manager_poll(void)
{
    /* Bluedroid事件驱动，无需轮询 */
}

uint16_t ble_manager_get_conn_id(void)
{
    return s_conn_id;
}

esp_err_t ble_manager_disconnect(void)
{
    if (!s_ble_connected || s_conn_id == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_ble_gatts_close(s_gatts_if, s_conn_id);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_manager_send_time_sync_response(uint32_t timestamp)
{
    uint8_t buffer[64];
    size_t packet_length = bipupu_protocol_create_time_sync(timestamp, buffer, sizeof(buffer));

    if (packet_length == 0) {
        return ESP_FAIL;
    }

    esp_err_t ret = nus_tx_notify(buffer, packet_length);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

void ble_manager_force_reset_bonds(void)
{
    clear_binding_info();

    if (s_ble_connected && s_conn_id != 0xFFFF) {
        esp_ble_gatts_close(s_gatts_if, s_conn_id);
        ble_is_connected = false;
    }

    s_is_bound = false;
    memset(s_bound_device_addr, 0, sizeof(s_bound_device_addr));
    memset(s_bound_device_name, 0, sizeof(s_bound_device_name));
}
