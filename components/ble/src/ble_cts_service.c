/**
 * @file ble_cts_service.c
 * @brief Current Time Service (CTS) 实现 (原生 NimBLE 版本)
 * 
 * 实现蓝牙标准 CTS 服务，支持时间同步。
 * 时间格式: Exact Time 256 (10 字节)
 */

#include "ble_cts_service.h"
#include "ble_config.h"
#include "ble_protocol.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_cts";

/* ================== UUID 定义 ================== */

// CTS Service UUID: 0x1805
static const ble_uuid16_t cts_svc_uuid = BLE_UUID16_INIT(CTS_SERVICE_UUID);

// Current Time Characteristic UUID: 0x2A2B
static const ble_uuid16_t cts_time_chr_uuid = BLE_UUID16_INIT(CTS_CURRENT_TIME_UUID);

// Local Time Info Characteristic UUID: 0x2A0F
static const ble_uuid16_t cts_local_time_chr_uuid = BLE_UUID16_INIT(CTS_LOCAL_TIME_INFO_UUID);

/* ================== 私有状态 ================== */
static uint16_t s_time_chr_val_handle;       // Current Time 特征值句柄
static uint16_t s_local_time_chr_val_handle; // Local Time Info 特征值句柄
static ble_cts_time_callback_t s_time_callback = NULL;
static bool s_notify_enabled = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// 当前时间数据缓存
static uint8_t s_current_time_data[10] = {0};

// Local Time Info: UTC+8 (中国标准时间)
static uint8_t s_local_time_info[2] = {32, 0};  // offset=32 (8*4), DST=0

/* ================== 私有函数 ================== */

/**
 * @brief 处理 CTS 时间写入
 */
static void handle_time_write(const uint8_t* data, uint16_t len)
{
    if (!data || len < 10) {
        ESP_LOGW(TAG, "CTS write data too short: %d bytes", len);
        return;
    }

    ble_cts_time_t cts_time;
    if (ble_protocol_parse_cts_time(data, len, &cts_time)) {
        ESP_LOGI(TAG, "Time sync received: %04d-%02d-%02d %02d:%02d:%02d",
                 cts_time.year, cts_time.month, cts_time.day,
                 cts_time.hour, cts_time.minute, cts_time.second);

        // 缓存时间数据
        memcpy(s_current_time_data, data, 10);

        if (s_time_callback) {
            s_time_callback(&cts_time);
        }
    }
}

/* ================== GATT 访问回调 ================== */

static int cts_time_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 返回缓存的时间数据
        rc = os_mbuf_append(ctxt->om, s_current_time_data, sizeof(s_current_time_data));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        ESP_LOGD(TAG, "CTS time read request");
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        // 手机写入时间数据
        if (ctxt->om) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t buf[16];
            
            if (om_len > sizeof(buf)) {
                om_len = sizeof(buf);
            }
            
            rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
            if (rc == 0) {
                handle_time_write(buf, om_len);
            }
        }
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int cts_local_time_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 返回时区信息
        rc = os_mbuf_append(ctxt->om, s_local_time_info, sizeof(s_local_time_info));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        ESP_LOGD(TAG, "Local time info read request");
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* ================== GATT 服务定义 ================== */

static const struct ble_gatt_svc_def cts_svc_def[] = {
    {
        // CTS Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &cts_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Current Time Characteristic
                .uuid = &cts_time_chr_uuid.u,
                .access_cb = cts_time_chr_access,
                .val_handle = &s_time_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Local Time Info Characteristic (只读)
                .uuid = &cts_local_time_chr_uuid.u,
                .access_cb = cts_local_time_chr_access,
                .val_handle = &s_local_time_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                0, // 终止符
            },
        },
    },
    {
        0, // 终止符
    },
};

/* ================== 公开接口实现 ================== */

int ble_cts_service_init(void)
{
    int rc;

    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    memset(s_current_time_data, 0, sizeof(s_current_time_data));

    // 注册 GATT 服务
    rc = ble_gatts_count_cfg(cts_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count CTS service config; rc=%d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(cts_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add CTS service; rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "CTS service initialized (Native NimBLE)");
    return 0;
}

void ble_cts_service_deinit(void)
{
    s_time_callback = NULL;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGI(TAG, "CTS service deinitialized");
}

void ble_cts_service_set_time_callback(ble_cts_time_callback_t callback)
{
    s_time_callback = callback;
}

esp_err_t ble_cts_service_notify_time(uint16_t conn_handle, const ble_cts_time_t* time_info)
{
    if (s_time_chr_val_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[10];
    uint16_t len = sizeof(data);

    if (!ble_protocol_create_cts_response(time_info, data, &len)) {
        return ESP_FAIL;
    }

    // 构建 mbuf
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }

    // 发送通知
    int rc = ble_gatts_notify_custom(conn_handle, s_time_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send time notification; rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void ble_cts_service_set_conn_handle(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
}

uint16_t ble_cts_service_get_time_handle(void)
{
    return s_time_chr_val_handle;
}
