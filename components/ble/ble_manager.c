/**
 * @file ble_manager.c
 * @brief BLE 管理器实现 (Bipupu 协议版本 1.2)
 * 
 * 基于 Nordic UART Service (NUS) 实现 Bipupu 蓝牙协议
 * 协议格式: [协议头(0xB0)][时间戳(4)][消息类型(1)][数据长度(2)][数据(N)][校验和(1)]
 */

#include "ble_manager.h"
#include "bipupu_protocol.h"
#include "board.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>


// --- 绑定相关常量 ---
#define BINDING_NVS_NAMESPACE "ble_binding"
#define BINDING_NVS_KEY "bound_device"
#define BINDING_NVS_NAME_KEY "bound_name"

// --- 全局状态 ---
static const char *TAG = "BLE_MANAGER";
static char s_bound_device_addr[18] = {0}; // 存储绑定的设备地址
static char s_bound_device_name[32] = {0}; // 存储绑定的设备名称
static bool s_is_bound = false; // 是否已绑定

// 绑定相关函数声明
static void handle_binding_packet(const uint8_t* data, size_t length);
static void save_binding_info(void);
static void clear_binding_info(void);
static void load_binding_info(void);
static bool check_binding_match(void);

/* ================== 私有常量定义 ================== */

/** Nordic UART Service 标准 128-bit UUID
 *
 *  服务:  6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *  RX特征(手机→设备,写入): 6e400002-b5a3-f393-e0a9-e50e24dcca9e
 *  TX特征(设备→手机,通知): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
 *
 *  字节序：小端序（NimBLE 约定）
 */
static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

/** NUS RX 特征值：手机 -> 设备 (写入)，6e400002 */
static const ble_uuid128_t nus_rx_char_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

/** NUS TX 特征值：设备 -> 手机 (通知)，6e400003 */
static const ble_uuid128_t nus_tx_char_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/** 设备名称 */
#define DEVICE_NAME_PREFIX     "Bipupu_"

/** 广播参数 */
#define ADV_INTERVAL_MIN_MS    500     /**< 最小广播间隔，对孕呼机设备无需短间隔 */
#define ADV_INTERVAL_MAX_MS    1000   /**< 最大广播间隔 */
#define ADV_DURATION_SEC       0       /**< 0 = 永久广播 */

/* ================== 私有状态变量 ================== */

/** 蓝牙管理器状态 */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;

/** 连接状态 */
static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0xFFFF;  /**< BLE_HS_CONN_HANDLE_NONE */
static uint8_t s_own_addr_type;

/** 外部可见的连接状态 */
bool ble_is_connected = false;

/** 错误计数 */
static uint32_t s_error_count = 0;

/** 回调函数 */
static ble_message_callback_t s_message_callback = NULL;
static ble_time_sync_callback_t s_time_sync_callback = NULL;
static ble_connection_callback_t s_connection_callback = NULL;

/* ================== 私有函数声明 ================== */
static void handle_time_sync_directly(uint32_t timestamp);

/** NUS 服务句柄 */
/** NUS RX 特征句柄 (6e400002): 手机写入 → 设备接收 */
static uint16_t s_nus_rx_val_handle;
/** NUS TX 特征句柄 (6e400003): 设备通知 → 手机接收 */
static uint16_t s_nus_tx_val_handle;

/** 设备名称缓冲区 */
static char s_device_name[32];

/* ================== 前向声明 ================== */

static void ble_host_task(void *param);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_advertise(void);
static int nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ================== NUS 服务定义 ================== */

/** NUS 服务定义（使用标准 128-bit UUID） */
static const struct ble_gatt_svc_def nus_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* NUS RX: 手机→设备 写入特征值 (6e400002) */
                .uuid = &nus_rx_char_uuid.u,
                .access_cb = nus_rx_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_nus_rx_val_handle,
            },
            {
                /* NUS TX: 设备→手机 通知特征值 (6e400003) */
                .uuid = &nus_tx_char_uuid.u,
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_nus_tx_val_handle,
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

    /* ── 绑定安全校验 ─────────────────────────────────────────────
     * 设备已绑定后，只接受来自绑定手机的协议包（绑定/解绑指令本身除外）。
     * 防止附近陌生蓝牙设备恐恨地向存质器发送消息或修改时间。
     */
    if (s_is_bound) {
        bipupu_message_type_t mt = packet.message_type;
        bool is_bind_cmd = (mt == BIPUPU_MSG_BINDING_INFO ||
                            mt == BIPUPU_MSG_UNBIND_COMMAND);
        if (!is_bind_cmd && !check_binding_match()) {
            ESP_LOGW(TAG, "拒绝来自非绑定设备的数据包 (type=0x%02X)", mt);
            s_error_count++;
            return;
        }
    }

    // 根据消息类型处理
    switch (packet.message_type) {
        case BIPUPU_MSG_TIME_SYNC:
            ESP_LOGI(TAG, "收到时间同步消息: timestamp=%u", packet.timestamp);
            
            // 直接处理时间同步，调用硬件接口
            handle_time_sync_directly(packet.timestamp);
            
            // 仍然调用回调函数，保持向后兼容
            if (s_time_sync_callback) {
                s_time_sync_callback(packet.timestamp);
            }
            break;
            
        case BIPUPU_MSG_TEXT:
            ESP_LOGI(TAG, "收到文本消息 [%s]: %s (ts=%u)",
                    packet.sender_name, packet.body_text, packet.timestamp);
            if (s_message_callback) {
                s_message_callback(packet.sender_name, packet.body_text, packet.timestamp);
            }
            break;
            
        case BIPUPU_MSG_ACKNOWLEDGEMENT:
            ESP_LOGI(TAG, "收到确认响应: timestamp=%u", packet.timestamp);
            // 暂时不处理确认响应
            break;
            
        case BIPUPU_MSG_BINDING_INFO:
        case BIPUPU_MSG_UNBIND_COMMAND:
            ESP_LOGI(TAG, "收到绑定相关消息: type=0x%02X", packet.message_type);
            handle_binding_packet(data, length);
            break;
            
        default:
            ESP_LOGW(TAG, "未知消息类型: 0x%02X", packet.message_type);
            break;
    }
}

/**
 * @brief NUS RX 特征值写入回调（手机 → 设备）
 */
static int nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
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
 * @brief 处理绑定相关数据包
 */
static void handle_binding_packet(const uint8_t* data, size_t length)
{
    bipupu_parsed_packet_t parsed;
    if (!bipupu_protocol_parse(data, length, &parsed)) {
        ESP_LOGE(TAG, "解析绑定数据包失败");
        return;
    }
    
    switch (parsed.message_type) {
        case BIPUPU_MSG_BINDING_INFO:
            ESP_LOGI(TAG, "收到绑定信息");
            
            // 解析绑定信息
            char json_str[256];
            bipupu_protocol_decode_utf8_safe(parsed.data, parsed.data_length, json_str);
            ESP_LOGI(TAG, "收到绑定信息: %s", json_str);
            
            // 保存绑定信息到NVS
            save_binding_info();
            break;
            
        case BIPUPU_MSG_UNBIND_COMMAND:
            ESP_LOGI(TAG, "收到解绑命令");
            
            // 清除绑定信息
            clear_binding_info();
            
            // 断开连接
            if (s_ble_connected && s_conn_handle != 0xFFFF) {
                ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief 保存绑定信息到NVS
 */
static void save_binding_info(void)
{
    // 获取当前连接的设备地址
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(s_conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGE(TAG, "获取连接信息失败: %d", rc);
        return;
    }
    
    // 格式化设备地址
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
    
    // 保存到NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(err));
        return;
    }
    
    err = nvs_set_str(nvs_handle, BINDING_NVS_KEY, addr_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存绑定地址失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    // 保存设备名称（如果有）
    if (strlen(s_bound_device_name) > 0) {
        err = nvs_set_str(nvs_handle, BINDING_NVS_NAME_KEY, s_bound_device_name);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "保存设备名称失败: %s", esp_err_to_name(err));
        }
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        strncpy(s_bound_device_addr, addr_str, sizeof(s_bound_device_addr) - 1);
        s_is_bound = true;
        ESP_LOGI(TAG, "绑定信息已保存: %s", addr_str);
    }
}

/**
 * @brief 清除绑定信息
 */
static void clear_binding_info(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(err));
        return;
    }
    
    err = nvs_erase_key(nvs_handle, BINDING_NVS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "清除绑定地址失败: %s", esp_err_to_name(err));
    }
    
    err = nvs_erase_key(nvs_handle, BINDING_NVS_NAME_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "清除设备名称失败: %s", esp_err_to_name(err));
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        memset(s_bound_device_addr, 0, sizeof(s_bound_device_addr));
        memset(s_bound_device_name, 0, sizeof(s_bound_device_name));
        s_is_bound = false;
        ESP_LOGI(TAG, "绑定信息已清除");
    }
}

/**
 * @brief 加载绑定信息
 */
static void load_binding_info(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BINDING_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开NVS失败或没有绑定信息: %s", esp_err_to_name(err));
        return;
    }
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, BINDING_NVS_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        err = nvs_get_str(nvs_handle, BINDING_NVS_KEY, s_bound_device_addr, &required_size);
        if (err == ESP_OK) {
            s_is_bound = true;
            ESP_LOGI(TAG, "加载绑定地址: %s", s_bound_device_addr);
        }
    }
    
    // 加载设备名称
    required_size = 0;
    err = nvs_get_str(nvs_handle, BINDING_NVS_NAME_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        err = nvs_get_str(nvs_handle, BINDING_NVS_NAME_KEY, s_bound_device_name, &required_size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "加载设备名称: %s", s_bound_device_name);
        }
    }
    
    nvs_close(nvs_handle);
}

/**
 * @brief 检查当前连接是否与绑定匹配
 */
static bool check_binding_match(void)
{
    if (!s_is_bound || !ble_is_connected) {
        return false;
    }
    
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(s_conn_handle, &desc);
    if (rc != 0) {
        return false;
    }
    
    char current_addr[18];
    snprintf(current_addr, sizeof(current_addr), "%02x:%02x:%02x:%02x:%02x:%02x",
             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
    
    return strcmp(current_addr, s_bound_device_addr) == 0;
}

/**
 * @brief 通过 NUS TX 特征值向手机发送通知（设备 → 手机）
 */
static esp_err_t nus_tx_notify(const uint8_t* data, size_t length)
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
    
    // 通过 NUS TX 特征值（s_nus_tx_val_handle）向订阅的手机发送通知
    // 注意：无论成功与否，ble_gattc_notify_custom 都会消耗 om，无需手动释放
    int rc = ble_gattc_notify_custom(s_conn_handle, s_nus_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败: %d", rc);
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
    
    // 更新设备名称 (GAP) - 确保同步
    ble_svc_gap_device_name_set(s_device_name);
    
    ESP_LOGI(TAG, "BLE 主机同步完成");
    update_ble_state(BLE_STATE_IDLE);
    
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
                ble_is_connected = true;
                s_conn_handle = event->connect.conn_handle;
                update_ble_state(BLE_STATE_CONNECTED);
    
                if (s_connection_callback) {
                    s_connection_callback(true);
                }
                
                // 检查绑定匹配
                if (check_binding_match()) {
                    ESP_LOGI(TAG, "连接设备与绑定设备匹配");
                } else if (s_is_bound) {
                    ESP_LOGW(TAG, "连接设备与绑定设备不匹配");
                }
            } else {
                ESP_LOGE(TAG, "连接失败: %d", event->connect.status);
                s_error_count++;
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            // 连接断开
            ESP_LOGI(TAG, "设备已断开连接: reason=%d", event->disconnect.reason);
            ESP_LOGI(TAG, "设备已断开连接");
            s_ble_connected = false;
            ble_is_connected = false;
            s_conn_handle = 0xFFFF;
            update_ble_state(BLE_STATE_IDLE);
    
            if (s_connection_callback) {
                s_connection_callback(false);
            }
            
            // 重新开始广播
            ble_advertise();
            break;
            
        case BLE_GAP_EVENT_ADV_COMPLETE:
            // 广播完成
            ESP_LOGI(TAG, "广播完成");
            update_ble_state(BLE_STATE_IDLE);
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
    s_ble_state = BLE_STATE_IDLE; // 临时设置为空闲状态

    // NVS 已由 main.c 的 init_nvs() 完成初始化，此处无需重复调用。
    // 若在此处再次调用并触发擦除分支，将丢失已保存的绑定信息。

    // 初始化NimBLE
    esp_err_t ret = esp_nimble_hci_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE HCI初始化失败: %s", esp_err_to_name(ret));
        update_ble_state(BLE_STATE_ERROR);
        // HCI 初始化失败通常需要重启解决，或者检查分区/硬件
        return ret;
    }
    
    nimble_port_init();
    
    // 初始化 GAP 和 GATT 服务 (必须在 nimble_port_init 之后)
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    // 注册自定义 GATT 服务 (NUS)
    int rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT服务计数失败: %d", rc);
        esp_nimble_hci_deinit();
        update_ble_state(BLE_STATE_ERROR);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "添加GATT服务失败: %d", rc);
        esp_nimble_hci_deinit();
        update_ble_state(BLE_STATE_ERROR);
        return ESP_FAIL;
    }

    // 配置BLE主机回调
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    // 设置设备名称 (GAP Service)
    generate_device_name();
    ble_svc_gap_device_name_set(s_device_name);
    
    // 加载绑定信息
    load_binding_info();
    
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
    if (s_ble_state != BLE_STATE_IDLE) {
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
    
    update_ble_state(BLE_STATE_IDLE);
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

/**
 * @brief 直接处理时间同步消息
 * @param timestamp Unix时间戳 (秒)
 * 
 * 此函数在蓝牙层直接处理时间同步，调用硬件接口设置RTC时间
 */
static void handle_time_sync_directly(uint32_t timestamp)
{
    ESP_LOGI(TAG, "直接处理时间同步: timestamp=%u", timestamp);
    
    // 将Unix时间戳转换为年月日时分秒
    time_t time_val = (time_t)timestamp;
    struct tm *timeinfo = localtime(&time_val);
    
    if (!timeinfo) {
        ESP_LOGE(TAG, "时间戳转换失败");
        return;
    }
    
    ESP_LOGI(TAG, "转换后时间：%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    // 直接调用硬件接口设置RTC时间
    esp_err_t ret = board_set_rtc(
        timeinfo->tm_year + 1900, 
        timeinfo->tm_mon + 1, 
        timeinfo->tm_mday,
        timeinfo->tm_hour, 
        timeinfo->tm_min, 
        timeinfo->tm_sec
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC 已成功更新（蓝牙层直接处理）");
        board_notify();  // 通知用户时间已更新
    } else {
        ESP_LOGE(TAG, "RTC 更新失败：%s", esp_err_to_name(ret));
    }
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
    esp_err_t ret = nus_tx_notify(buffer, packet_length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送时间同步响应失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "时间同步响应发送成功: timestamp=%u", timestamp);
    return ESP_OK;
}


/**
 * @brief 强制清除所有绑定信息并断开连接
 * 用于本地按键解绑逻辑
 */
void ble_manager_force_reset_bonds(void)
{
    ESP_LOGI(TAG, "强制清除绑定信息");
    
    // 清除NVS中的绑定信息
    clear_binding_info();
    
    // 如果当前有连接，断开连接
    if (s_ble_connected && s_conn_handle != 0xFFFF) {
        ESP_LOGI(TAG, "断开当前连接");
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_is_connected = false;
    }
    
    // 重置绑定状态
    s_is_bound = false;
    memset(s_bound_device_addr, 0, sizeof(s_bound_device_addr));
    memset(s_bound_device_name, 0, sizeof(s_bound_device_name));
    
    ESP_LOGI(TAG, "绑定信息已强制清除");
}
