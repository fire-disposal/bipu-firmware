/**
 * @file ble_manager.c
 * @brief BLE 管理器实现 (原生 NimBLE 版本)
 * 
 * 使用原生 NimBLE API 管理 BLE 栈和所有 GATT 服务：
 * - Nordic UART Service (NUS): 消息通信
 * - Battery Service: 电池电量
 * - Current Time Service (CTS): 时间同步
 */

#include "ble_manager.h"
#include "ble_nus_service.h"
#include "ble_battery_service.h"
#include "ble_cts_service.h"
#include "ble_protocol.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nvs_flash.h"
#include "storage.h"
#include <string.h>

static const char* TAG = "ble_manager";

/* ================== 私有状态 ================== */
static ble_state_t s_ble_state = BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_error_count = 0;
static uint8_t s_own_addr_type;

// 回调函数
static ble_message_callback_t s_message_callback = NULL;
static ble_cts_time_callback_t s_cts_time_callback = NULL;

/* ================== 前向声明 ================== */
static void ble_host_task(void *param);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_advertise(void);

/* ================== NUS 消息回调适配 ================== */

/**
 * @brief NUS 消息回调，将原始文本转换为解析后的消息
 */
static void nus_message_handler(const char* message, uint16_t len)
{
    if (!s_message_callback || !message) return;

    ble_parsed_msg_t parsed;
    if (ble_protocol_parse_text(message, len, &parsed)) {
        s_message_callback(parsed.sender, parsed.message, &parsed.effect);
    }
}

/* ================== 辅助函数 ================== */

static void ble_handle_error(const char* operation, int error)
{
    s_error_count++;
    s_ble_state = BLE_STATE_ERROR;
    ESP_LOGE(TAG, "BLE error - Operation: %s, Code: %d", operation, error);
}

const char* ble_manager_state_to_string(ble_state_t state)
{
    switch (state) {
        case BLE_STATE_UNINITIALIZED: return "Uninitialized";
        case BLE_STATE_INITIALIZING:  return "Initializing";
        case BLE_STATE_INITIALIZED:   return "Initialized";
        case BLE_STATE_ADVERTISING:   return "Advertising";
        case BLE_STATE_CONNECTED:     return "Connected";
        case BLE_STATE_ERROR:         return "Error";
        default: return "Unknown";
    }
}

/**
 * @brief 打印设备地址
 */
static void print_addr(const void *addr)
{
    const uint8_t *u8p = addr;
    ESP_LOGI(TAG, "Device address: %02x:%02x:%02x:%02x:%02x:%02x",
             u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

/* ================== GAP 事件处理 ================== */

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        
        if (event->connect.status == 0) {
            s_ble_connected = true;
            s_conn_handle = event->connect.conn_handle;
            s_ble_state = BLE_STATE_CONNECTED;
            
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                char addr_str[32];
                snprintf(addr_str, sizeof(addr_str), 
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         desc.peer_ota_addr.val[5], desc.peer_ota_addr.val[4],
                         desc.peer_ota_addr.val[3], desc.peer_ota_addr.val[2],
                         desc.peer_ota_addr.val[1], desc.peer_ota_addr.val[0]);
                storage_save_ble_addr(addr_str);
                ESP_LOGI(TAG, "Peer address: %s", addr_str);
            }
            
            // 更新连接参数
            struct ble_gap_upd_params params = {
                .itvl_min = BLE_CONN_INT_MIN,
                .itvl_max = BLE_CONN_INT_MAX,
                .latency = BLE_CONN_LATENCY,
                .supervision_timeout = BLE_CONN_TIMEOUT,
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            ble_gap_update_params(event->connect.conn_handle, &params);
        } else {
            // 连接失败，重新广播
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        s_ble_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble_state = BLE_STATE_ADVERTISING;
        
        // 重新开始广播
        ble_advertise();
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        if (event->adv_complete.reason == 0) {
            // 广播超时，重新启动
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d reason=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason);
        break;

    default:
        ESP_LOGD(TAG, "GAP event: %d", event->type);
        break;
    }

    return 0;
}

/* ================== 广播配置 ================== */

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    // 广播标志: 通用发现模式, 仅 BLE
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 设备名称
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    // 设置广播数据
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    // 扫描响应中添加 NUS 服务 UUID
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    
    // NUS 服务 UUID (128-bit)
    static ble_uuid128_t nus_uuid = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
        0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
    );
    rsp_fields.uuids128 = &nus_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data; rc=%d", rc);
        // 继续，不是致命错误
    }

    // 广播参数
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // 可连接
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // 通用发现
    adv_params.itvl_min = BLE_ADV_INTERVAL_MIN;
    adv_params.itvl_max = BLE_ADV_INTERVAL_MAX;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
        return;
    }

    s_ble_state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "Advertising started");
}

/* ================== NimBLE 主机回调 ================== */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
    s_ble_state = BLE_STATE_ERROR;
}

static void ble_on_sync(void)
{
    int rc;

    // 确定地址类型
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error ensuring address; rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    // 打印设备地址
    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    print_addr(addr_val);

    s_ble_state = BLE_STATE_INITIALIZED;
    ESP_LOGI(TAG, "BLE host synchronized");

    // 开始广播
    ble_advertise();
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ================== GATT 服务注册 ================== */

/**
 * @brief 注册所有 GATT 服务
 */
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered characteristic %s with "
                 "def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registered descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

static int gatt_svr_init(void)
{
    int rc;

    // 初始化标准 GAP 和 GATT 服务
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // 初始化自定义服务
    rc = ble_nus_service_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init NUS service; rc=%d", rc);
        return rc;
    }
    ble_nus_service_set_callback(nus_message_handler);

    rc = ble_battery_service_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init Battery service; rc=%d", rc);
        return rc;
    }

    rc = ble_cts_service_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init CTS service; rc=%d", rc);
        return rc;
    }

    // 设置设备名称
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name; rc=%d", rc);
        return rc;
    }

    return 0;
}

/* ================== BLE 管理接口实现 ================== */

esp_err_t ble_manager_init(void)
{
    int rc;

    if (s_ble_state == BLE_STATE_ERROR) {
        ESP_LOGW(TAG, "BLE in error state, attempting reinit...");
        ble_manager_deinit();
        s_ble_state = BLE_STATE_UNINITIALIZED;
    }
    
    if (s_ble_state != BLE_STATE_UNINITIALIZED) {
        return ESP_OK;
    }
    
    s_ble_state = BLE_STATE_INITIALIZING;

    // 初始化 NimBLE 主机
    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble port; rc=%d", rc);
        ble_handle_error("nimble_port_init", rc);
        return ESP_FAIL;
    }

    // 配置主机回调
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // 初始化 GATT 服务
    rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to init GATT server; rc=%d", rc);
        ble_handle_error("gatt_svr_init", rc);
        return ESP_FAIL;
    }

    // 启动 NimBLE 主机任务
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Manager initialized successfully (Native NimBLE)");
    return ESP_OK;
}

esp_err_t ble_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE Manager...");

    // 停止广播
    ble_gap_adv_stop();

    // 断开连接
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    // 反初始化服务
    ble_nus_service_deinit();
    ble_battery_service_deinit();
    ble_cts_service_deinit();

    // 停止 NimBLE
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    }

    s_ble_state = BLE_STATE_UNINITIALIZED;
    s_ble_connected = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    ESP_LOGI(TAG, "BLE Manager deinitialized");
    return ESP_OK;
}

esp_err_t ble_manager_start_advertising(void)
{
    if (s_ble_state == BLE_STATE_UNINITIALIZED || 
        s_ble_state == BLE_STATE_INITIALIZING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_ble_connected) {
        ESP_LOGW(TAG, "Already connected, not advertising");
        return ESP_OK;
    }

    ble_advertise();
    return ESP_OK;
}

esp_err_t ble_manager_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising stopped");
        if (s_ble_state == BLE_STATE_ADVERTISING) {
            s_ble_state = BLE_STATE_INITIALIZED;
        }
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to stop advertising; rc=%d", rc);
    return ESP_FAIL;
}

void ble_manager_set_message_callback(ble_message_callback_t callback)
{
    s_message_callback = callback;
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
    // NimBLE 使用事件驱动，无需轮询
}

void ble_manager_update_battery_level(uint8_t level)
{
    ble_battery_service_update_level(level);
}

uint16_t ble_manager_get_conn_id(void)
{
    return s_conn_handle;
}
