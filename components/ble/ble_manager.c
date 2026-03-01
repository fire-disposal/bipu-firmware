/**
 * @file ble_manager.c
 * @brief BLE 管理器实现 (Bipupu 协议版本 1.2)
 * 
 * 基于 Nordic UART Service (NUS) 实现 Bipupu 蓝牙协议
 * 协议格式: [协议头(0xB0)][时间戳(4)][消息类型(1)][数据长度(2)][数据(N)][校验和(1)]
 */

#include "ble_manager.h"
#include "bipupu_protocol.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

// --- 全局状态 ---
static const char *TAG = "BLE_MANAGER";
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t addr_type;
bool ble_is_connected = false; // 暴露给外部模块
static ble_state_t current_state = BLE_STATE_IDLE;
static ble_message_callback_t message_callback = NULL;

/* ================== 私有常量定义 ================== */

/** Nordic UART Service UUIDs */
#define NUS_SERVICE_UUID       0x0001
#define NUS_TX_CHAR_UUID       0x0002  /**< 手机 -> 设备 (写) */
#define NUS_RX_CHAR_UUID       0x0003  /**< 设备 -> 手机 (通知) */

/** 设备名称 */
#define DEVICE_NAME_PREFIX     "Bipupu_"

/** 广播参数 */
#define ADV_INTERVAL_MIN_MS    100
#define ADV_INTERVAL_MAX_MS    200
#define ADV_DURATION_SEC       0       /**< 0 = 永久广播 */

/* ================== 私有状态变量 ================== */

/** 蓝牙管理器状态 */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;

/** 连接状态 */
static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0xFFFF;  /**< BLE_HS_CONN_HANDLE_NONE */

/** 设备地址类型 */
static uint8_t s_own_addr_type;

/** 错误计数 */
static uint32_t s_error_count = 0;

/** 回调函数 */
static ble_message_callback_t s_message_callback = NULL;
static ble_time_sync_callback_t s_time_sync_callback = NULL;
static ble_connection_callback_t s_connection_callback = NULL;

/** NUS 服务句柄 */
static uint16_t s_nus_service_handle;
static uint16_t s_nus_tx_char_handle;
static uint16_t s_nus_rx_char_handle;

/** 设备名称缓冲区 */
static char s_device_name[32];

/* ================== 前向声明 ================== */

static void ble_host_task(void *param);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_advertise(void);
static int nus_tx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ================== NUS 服务定义 ================== */

/** NUS 服务定义 */
static const struct ble_gatt_svc_def nus_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(NUS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(NUS_TX_CHAR_UUID),
                .access_cb = nus_tx_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_nus_tx_char_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(NUS_RX_CHAR_UUID),
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_nus_rx_char_handle,
            },
            { 0 }  /* 结束标记 */
        }
    },
    { 0 }  /* 结束标记 */
};

/* ================== 辅助函数 ================== */

/**
 * @brief 生成设备名称
 */
static void generate_device_name(void)
{
    // 获取设备MAC地址最后3个字节
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    // 格式: Bipupu_XXYYZZ
    snprintf(s_device_name, sizeof(s_device_name), 
             "%s%02X%02X%02X", DEVICE_NAME_PREFIX, 
             mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "设备名称: %s", s_device_name);
}

/**
 * @brief 更新蓝牙状态
 */
static void update_ble_state(ble_state_t new_state)
{
    if (s_ble_state != new_state) {
        ESP_LOGI(TAG, "蓝牙状态变更: %d -> %d", s_ble_state, new_state);
        s_ble_state = new_state;
    }
}

/**
 * @brief 处理接收到的数据包
 */
static void handle_received_packet(const uint8_t* data, size_t length)
{
    bipupu_parsed_packet_t packet;
    
    // 解析数据包
    if (!bipupu_protocol_parse(data, length, &packet)) {
        ESP_LOGW(TAG, "数据包解析失败");
        s_error_count++;
        return;
    }
    
    // 根据消息类型处理
    switch (packet.message_type) {
        case BIPUPU_MSG_TIME_SYNC:
            ESP_LOGI(TAG, "收到时间同步消息: timestamp=%u", packet.timestamp);
            if (s_time_sync_callback) {
                s_time_sync_callback(packet.timestamp);
            }
            break;
            
        case BIPUPU_MSG_TEXT:
            ESP_LOGI(TAG, "收到文本消息: timestamp=%u, text=%s", 
                    packet.timestamp, packet.text);
            if (s_message_callback) {
                // 默认发送者为 "App"
                s_message_callback("App", packet.text, packet.timestamp);
            }
            break;
            
        case BIPUPU_MSG_ACKNOWLEDGEMENT:
            ESP_LOGI(TAG, "收到确认响应: timestamp=%u", packet.timestamp);
            // 暂时不处理确认响应
            break;
            
        default:
            ESP_LOGW(TAG, "未知消息类型: 0x%02X", packet.message_type);
            break;
    }
}

/**
 * @brief NUS TX 特征值写回调
 */
static int nus_tx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    
    // 获取写入的数据
    struct os_mbuf *om = ctxt->om;
    size_t length = OS_MBUF_PKTLEN(om);
    
    if (length == 0) {
        return 0;
    }
    
    // 分配缓冲区并复制数据
    uint8_t *data = malloc(length);
    if (!data) {
        ESP_LOGE(TAG, "内存分配失败");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    uint16_t extracted_length;
    int rc = ble_hs_mbuf_to_flat(om, data, length, &extracted_length);
    length = extracted_length;
    if (rc != 0) {
        free(data);
        ESP_LOGE(TAG, "数据提取失败: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    
    ESP_LOGI(TAG, "收到 %zu 字节数据", length);
    
    // 处理接收到的数据
    handle_received_packet(data, length);
    
    free(data);
    return 0;
}

/**
 * @brief 通过NUS RX特征发送数据
 */
static esp_err_t send_data_via_nus_rx(const uint8_t* data, size_t length)
{
    if (!s_ble_connected || s_conn_handle == 0xFFFF) {
        ESP_LOGW(TAG, "未连接，无法发送数据");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 创建mbuf
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    if (!om) {
        ESP_LOGE(TAG, "创建mbuf失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发送通知
    int rc = ble_gattc_notify_custom(s_conn_handle, s_nus_rx_char_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败: %d", rc);
        os_mbuf_free_chain(om);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "成功发送 %zu 字节数据", length);
    return ESP_OK;
}

/* ================== NimBLE 回调函数 ================== */

/**
 * @brief BLE 主机重置回调
 */
static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE 主机重置，原因: %d", reason);
    s_error_count++;
    update_ble_state(BLE_STATE_ERROR);
}

/**
 * @brief BLE 主机同步完成回调
 */
static void ble_on_sync(void)
{
    int rc;
    
    // 获取设备地址类型
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "获取地址类型失败: %d", rc);
        return;
    }
    
    // 生成设备名称
    generate_device_name();
    
    // 设置设备名称
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置设备名称失败: %d", rc);
        return;
    }
    
    // 注册GATT服务
    rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT服务计数失败: %d", rc);
        return;
    }
    
    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "添加GATT服务失败: %d", rc);
        return;
    }
    
    ESP_LOGI(TAG, "BLE 主机同步完成");
    update_ble_state(BLE_STATE_INITIALIZED);
    
    // 开始广播
    ble_advertise();
}

/**
 * @brief GAP 事件处理
 */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // 连接建立
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "设备已连接");
                s_ble_connected = true;
                s_conn_handle = event->connect.conn_handle;
                update_ble_state(BLE_STATE_CONNECTED);
                
                if (s_connection_callback) {
                    s_connection_callback(true);
                }
            } else {
                ESP_LOGE(TAG, "连接失败: %d", event->connect.status);
                s_error_count++;
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            // 连接断开
            ESP_LOGI(TAG, "设备已断开连接: reason=%d", event->disconnect.reason);
            s_ble_connected = false;
            s_conn_handle = 0xFFFF;
            update_ble_state(BLE_STATE_INITIALIZED);
            
            if (s_connection_callback) {
                s_connection_callback(false);
            }
            
            // 重新开始广播
            ble_advertise();
            break;
            
        case BLE_GAP_EVENT_ADV_COMPLETE:
            // 广播完成
            ESP_LOGI(TAG, "广播完成");
            update_ble_state(BLE_STATE_INITIALIZED);
            break;
            
        case BLE_GAP_EVENT_SUBSCRIBE:
            // 特征值订阅状态变化
            ESP_LOGI(TAG, "特征值订阅状态变化: attr_handle=%d, subscribed=%d",
                    event->subscribe.attr_handle, 
                    event->subscribe.cur_notify);
            break;
            
        default:
            break;
    }
    
    return 0;
}

/**
 * @brief 开始蓝牙广播
 */
static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;
    
    // 配置广播数据
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    
    fields.name = (uint8_t*)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;
    
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播字段失败: %d", rc);
        return;
    }
    
    // 配置广播参数
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MIN_MS);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MAX_MS);
    
    // 开始广播
    rc = ble_gap_adv_start(s_own_addr_type, NULL, ADV_DURATION_SEC,
                          &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "开始广播失败: %d", rc);
        s_error_count++;
        return;
    }
    
    ESP_LOGI(TAG, "开始蓝牙广播");
    update_ble_state(BLE_STATE_ADVERTISING);
}

/**
 * @brief BLE 主机任务
 */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE 主机任务启动");
    nimble_port_run();
}

/* ================== 公共接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    ESP_LOGI(TAG, "初始化蓝牙管理器");
    
    if (s_ble_state != BLE_STATE_UNINITIALIZED) {
        ESP_LOGW(TAG, "蓝牙管理器已初始化，状态: %d", s_ble_state);
        return ESP_OK;
    }
    
    // 蓝牙管理器正在初始化
    s_ble_state = BLE_STATE_INITIALIZED; // 临时设置为已初始化状态
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS分区问题，尝试擦除并重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS初始化失败: %s", esp_err_to_name(ret));
        update_ble_state(BLE_STATE_ERROR);
        return ret;
    }
    
    // 初始化NimBLE
    ret = esp_nimble_hci_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE HCI初始化失败: %s", esp_err_to_name(ret));
        update_ble_state(BLE_STATE_ERROR);
        return ret;
    }
    
    nimble_port_init();
    
    // 配置BLE主机
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    // 设置设备名称
    generate_device_name();
    ble_svc_gap_device_name_set(s_device_name);
    
    // 启动BLE主机任务
    nimble_port_freertos_init(ble_host_task);
    
    ESP_LOGI(TAG, "蓝牙管理器初始化完成");
    return ESP_OK;
}

esp_err_t ble_manager_deinit(void)
{
    ESP_LOGI(TAG, "反初始化蓝牙管理器");
    
    if (s_ble_state == BLE_STATE_UNINITIALIZED) {
        return ESP_OK;
    }
    
    // 停止广播
    ble_gap_adv_stop();
    
    // 断开连接
    if (s_ble_connected && s_conn_handle != 0xFFFF) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    
    // 反初始化NimBLE
    nimble_port_deinit();
    
    update_ble_state(BLE_STATE_UNINITIALIZED);
    s_ble_connected = false;
    s_conn_handle = 0xFFFF;
    
    ESP_LOGI(TAG, "蓝牙管理器反初始化完成");
    return ESP_OK;
}

esp_err_t ble_manager_start_advertising(void)
{
    if (s_ble_state != BLE_STATE_INITIALIZED && s_ble_state != BLE_STATE_ERROR) {
        ESP_LOGW(TAG, "无效状态，无法开始广播: %d", s_ble_state);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_ble_connected) {
        ESP_LOGW(TAG, "已连接，无需广播");
        return ESP_OK;
    }
    
    ble_advertise();
    return ESP_OK;
}

esp_err_t ble_manager_stop_advertising(void)
{
    if (s_ble_state != BLE_STATE_ADVERTISING) {
        return ESP_OK;
    }
    
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "停止广播失败: %d", rc);
        return ESP_FAIL;
    }
    
    update_ble_state(BLE_STATE_INITIALIZED);
    ESP_LOGI(TAG, "蓝牙广播已停止");
    return ESP_OK;
}

void ble_manager_set_message_callback(ble_message_callback_t callback)
{
    s_message_callback = callback;
    ESP_LOGI(TAG, "消息回调已设置");
}

void ble_manager_set_time_sync_callback(ble_time_sync_callback_t callback)
{
    s_time_sync_callback = callback;
    ESP_LOGI(TAG, "时间同步回调已设置");
}

void ble_manager_set_connection_callback(ble_connection_callback_t callback)
{
    s_connection_callback = callback;
    ESP_LOGI(TAG, "连接状态回调已设置");
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
    // NimBLE 使用事件驱动，无需轮询
}

uint16_t ble_manager_get_conn_id(void)
{
    return s_conn_handle;
}

esp_err_t ble_manager_disconnect(void)
{
    if (!s_ble_connected || s_conn_handle == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "断开连接失败: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "连接已断开");
    return ESP_OK;
}

esp_err_t ble_manager_send_text_message(const char* text, size_t text_length)
{
    if (!text || text_length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取当前时间戳
    time_t now = time(NULL);
    if (now < 0) {
        now = 0;
    }
    
    // 创建数据包
    uint8_t buffer[512];
    size_t packet_length = bipupu_protocol_create_text_message(
        (uint32_t)now, text, text_length, buffer, sizeof(buffer));
    
    if (packet_length == 0) {
        ESP_LOGE(TAG, "创建文本消息数据包失败");
        return ESP_FAIL;
    }
    
    // 发送数据
    esp_err_t ret = send_data_via_nus_rx(buffer, packet_length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送文本消息失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "文本消息发送成功: %s", text);
    return ESP_OK;
}

esp_err_t ble_manager_send_time_sync_response(uint32_t timestamp)
{
    // 创建时间同步数据包
    uint8_t buffer[64];
    size_t packet_length = bipupu_protocol_create_time_sync(
        timestamp, buffer, sizeof(buffer));
    
    if (packet_length == 0) {
        ESP_LOGE(TAG, "创建时间同步数据包失败");
        return ESP_FAIL;
    }
    
    // 发送数据
    esp_err_t ret = send_data_via_nus_rx(buffer, packet_length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送时间同步响应失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "时间同步响应发送成功: timestamp=%u", timestamp);
    return ESP_OK;
}