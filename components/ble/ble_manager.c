/**
 * @file ble_manager.c
 * @brief BLE 管理器实现 (Bipupu 协议版本 1.2)
 *
 * 基于 Nordic UART Service (NUS) 实现 Bipupu 蓝牙协议
 * 协议格式：[协议头 (0xB0)][时间戳 (4)][消息类型 (1)][数据长度 (2)][数据 (N)][校验和 (1)]
 *
 * 自包含的 BLE 消息处理：
 *   - 在 NimBLE 任务中接收数据 → 入队（非阻塞）
 *   - 在 app_task 中 ble_manager_process_pending_messages() → 出队并调用回调
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
#include "freertos/queue.h"
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


/* ================== 全局状态 ================== */
static const char *TAG = "BLE";
static char s_bound_device_addr[18] = {0};
static char s_bound_device_name[32] = {0};
static bool s_is_bound = false;


/* ================== 绑定相关函数声明 ================== */
static void handle_binding_packet(const uint8_t* data, size_t length);
static void save_binding_info(void);
static void clear_binding_info(void);
static void load_binding_info(void);
static bool check_binding_match(void);


/* ================== 私有常量定义 ================== */

/** Nordic UART Service 标准 128-bit UUID
 *  服务：6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *  RX 特征 (手机→设备，写入): 6e400002-b5a3-f393-e0a9-e50e24dcca9e
 *  TX 特征 (设备→手机，通知): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
 */
static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

/** NUS RX 特征值：手机 -> 设备 (写入) */
static const ble_uuid128_t nus_rx_char_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

/** NUS TX 特征值：设备 -> 手机 (通知) */
static const ble_uuid128_t nus_tx_char_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

#define DEVICE_NAME_PREFIX     "Bipupu_"
#define ADV_INTERVAL_MIN_MS    500
#define ADV_INTERVAL_MAX_MS    1000
#define ADV_DURATION_SEC       0


/* ================== 私有状态变量 ================== */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0xFFFF;
static uint8_t s_own_addr_type;
static uint32_t s_error_count = 0;

/* 全局连接状态标识 */
bool ble_is_connected = false;

static ble_message_callback_t s_message_callback = NULL;
static ble_time_sync_callback_t s_time_sync_callback = NULL;
static ble_connection_callback_t s_connection_callback = NULL;


/* ================== 私有函数声明 ================== */
static void handle_time_sync_directly(uint32_t timestamp);
static void ble_host_task(void *param);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_advertise(void);
static int nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static void handle_received_packet(const uint8_t* data, size_t length);
static esp_err_t nus_tx_notify(const uint8_t* data, size_t length);
static void send_ack_response(uint32_t original_message_id);


/* ================== NUS 服务句柄 ================== */
static uint16_t s_nus_rx_val_handle;
static uint16_t s_nus_tx_val_handle;
static char s_device_name[32];


/* ================== NUS 服务定义 ================== */
static const struct ble_gatt_svc_def nus_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &nus_rx_char_uuid.u,
                .access_cb = nus_rx_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_nus_rx_val_handle,
            },
            {
                .uuid = &nus_tx_char_uuid.u,
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_nus_tx_val_handle,
            },
            { 0 }
        }
    },
    { 0 }
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
        s_ble_state = new_state;
    }
}

/** 处理接收到的数据包 */
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

/** NUS RX 特征值写入回调（手机 → 设备） */
static int nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    struct os_mbuf *om = ctxt->om;
    size_t length = OS_MBUF_PKTLEN(om);

    if (length == 0) {
        return 0;
    }

    uint8_t *data = malloc(length);
    if (!data) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    uint16_t extracted_length;
    int rc = ble_hs_mbuf_to_flat(om, data, length, &extracted_length);
    length = extracted_length;
    if (rc != 0) {
        free(data);
        return BLE_ATT_ERR_UNLIKELY;
    }

    handle_received_packet(data, length);
    free(data);
    return 0;
}

/** 发送 ACK 确认响应 */
static void send_ack_response(uint32_t original_message_id)
{
    if (!s_ble_connected || s_conn_handle == 0xFFFF) {
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

/** 处理绑定相关数据包 */
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
            if (s_ble_connected && s_conn_handle != 0xFFFF) {
                ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            break;

        default:
            break;
    }
}

/** 保存绑定信息到 NVS */
static void save_binding_info(void)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(s_conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "获取连接失败 %d", rc);
        return;
    }

    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);

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

/** 清除绑定信息 */
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

/** 加载绑定信息 */
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

/** 检查当前连接是否与绑定匹配 */
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

/** 通过 NUS TX 特征值发送通知 */
static esp_err_t nus_tx_notify(const uint8_t* data, size_t length)
{
    if (!s_ble_connected || s_conn_handle == 0xFFFF) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gattc_notify_custom(s_conn_handle, s_nus_tx_val_handle, om);
    if (rc != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}


/* ================== NimBLE 回调函数 ================== */

static void ble_on_reset(int reason)
{
    s_error_count++;
    update_ble_state(BLE_STATE_ERROR);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        return;
    }

    ble_svc_gap_device_name_set(s_device_name);
    update_ble_state(BLE_STATE_IDLE);
    ble_advertise();
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_ble_connected = true;
                ble_is_connected = true;
                s_conn_handle = event->connect.conn_handle;
                update_ble_state(BLE_STATE_CONNECTED);

                if (s_connection_callback) {
                    s_connection_callback(true);
                }
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            s_ble_connected = false;
            ble_is_connected = false;
            s_conn_handle = 0xFFFF;
            update_ble_state(BLE_STATE_IDLE);

            if (s_connection_callback) {
                s_connection_callback(false);
            }
            ble_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            update_ble_state(BLE_STATE_IDLE);
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            break;

        default:
            break;
    }

    return 0;
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t*)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MIN_MS);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MAX_MS);

    rc = ble_gap_adv_start(s_own_addr_type, NULL, ADV_DURATION_SEC,
                          &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        s_error_count++;
        return;
    }

    update_ble_state(BLE_STATE_ADVERTISING);
}

static void ble_host_task(void *param)
{
    nimble_port_run();
}


/* ================== 公共接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    if (s_ble_state != BLE_STATE_UNINITIALIZED) {
        return ESP_OK;
    }

    s_ble_state = BLE_STATE_IDLE;

    esp_err_t ret = esp_nimble_hci_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE HCI init failed: %s", esp_err_to_name(ret));
        update_ble_state(BLE_STATE_ERROR);
        return ret;
    }

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count cfg failed: %d", rc);
        esp_nimble_hci_deinit();
        update_ble_state(BLE_STATE_ERROR);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add svcs failed: %d", rc);
        esp_nimble_hci_deinit();
        update_ble_state(BLE_STATE_ERROR);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    generate_device_name();
    ble_svc_gap_device_name_set(s_device_name);
    load_binding_info();
    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

/** 初始化 BLE 消息队列 */
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

/** 处理待处理的 BLE 消息 */
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

    ble_gap_adv_stop();

    if (s_ble_connected && s_conn_handle != 0xFFFF) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    nimble_port_deinit();
    update_ble_state(BLE_STATE_UNINITIALIZED);
    s_ble_connected = false;
    s_conn_handle = 0xFFFF;

    return ESP_OK;
}

esp_err_t ble_manager_start_advertising(void)
{
    if (s_ble_state != BLE_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_connected) {
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
        return ESP_FAIL;
    }

    update_ble_state(BLE_STATE_IDLE);
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
    /* NimBLE 事件驱动，无需轮询 */
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

    if (s_ble_connected && s_conn_handle != 0xFFFF) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_is_connected = false;
    }

    s_is_bound = false;
    memset(s_bound_device_addr, 0, sizeof(s_bound_device_addr));
    memset(s_bound_device_name, 0, sizeof(s_bound_device_name));
}

/** 直接处理时间同步消息 */
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
