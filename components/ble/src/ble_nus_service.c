/**
 * @file ble_nus_service.c
 * @brief Nordic UART Service (NUS) 实现 (原生 NimBLE 版本)
 * 
 * 实现 NUS 服务，支持消息分块接收和发送。
 * 消息格式: 纯 UTF-8 文本，如 "From John: Hello World"
 */

#include "ble_nus_service.h"
#include "ble_config.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_nus";

/* ================== UUID 定义 ================== */

// NUS Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
);

// NUS TX Characteristic UUID: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E (手机写入)
static const ble_uuid128_t nus_tx_chr_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
);

// NUS RX Characteristic UUID: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E (设备发送)
static const ble_uuid128_t nus_rx_chr_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
);

/* ================== 私有状态 ================== */
static uint16_t s_rx_chr_val_handle;  // RX 特征值句柄 (用于 notify)
static ble_nus_message_callback_t s_callback = NULL;
static bool s_notify_enabled = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// 消息缓冲区（用于分块接收）
static uint8_t s_rx_buffer[BLE_MAX_MESSAGE_LEN];
static uint16_t s_rx_len = 0;

/* ================== 私有函数 ================== */

/**
 * @brief 处理接收到的数据
 */
static void handle_rx_data(const uint8_t* data, uint16_t len)
{
    if (!data || len == 0) return;

    // 追加到缓冲区
    if (s_rx_len + len > BLE_MAX_MESSAGE_LEN) {
        ESP_LOGW(TAG, "Message buffer overflow, resetting");
        s_rx_len = 0;
    }

    memcpy(s_rx_buffer + s_rx_len, data, len);
    s_rx_len += len;

    // 确保字符串以 null 结尾
    s_rx_buffer[s_rx_len] = '\0';

    ESP_LOGI(TAG, "Received message (%d bytes): %s", s_rx_len, s_rx_buffer);

    // 调用回调
    if (s_callback) {
        s_callback((const char*)s_rx_buffer, s_rx_len);
    }

    // 重置缓冲区
    s_rx_len = 0;
}

/* ================== GATT 访问回调 ================== */

static int nus_tx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        // 手机写入数据到设备
        if (ctxt->om) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t buf[BLE_MAX_MESSAGE_LEN];
            
            if (om_len > sizeof(buf)) {
                om_len = sizeof(buf);
            }
            
            rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
            if (rc == 0) {
                handle_rx_data(buf, om_len);
            }
        }
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int nus_rx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // RX 特征主要用于 Notify，读取时返回空
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 返回空数据
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* ================== GATT 服务定义 ================== */

static const struct ble_gatt_svc_def nus_svc_def[] = {
    {
        // NUS Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // TX Characteristic (手机写入)
                .uuid = &nus_tx_chr_uuid.u,
                .access_cb = nus_tx_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // RX Characteristic (设备发送, Notify)
                .uuid = &nus_rx_chr_uuid.u,
                .access_cb = nus_rx_chr_access,
                .val_handle = &s_rx_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
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

int ble_nus_service_init(void)
{
    int rc;

    s_rx_len = 0;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    // 注册 GATT 服务
    rc = ble_gatts_count_cfg(nus_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count NUS service config; rc=%d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(nus_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add NUS service; rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "NUS service initialized (Native NimBLE)");
    return 0;
}

void ble_nus_service_deinit(void)
{
    s_callback = NULL;
    s_rx_len = 0;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGI(TAG, "NUS service deinitialized");
}

void ble_nus_service_set_callback(ble_nus_message_callback_t callback)
{
    s_callback = callback;
}

esp_err_t ble_nus_service_send(uint16_t conn_handle, const uint8_t* data, uint16_t len)
{
    if (s_rx_chr_val_handle == 0) {
        ESP_LOGW(TAG, "RX characteristic not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // 构建 mbuf
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }

    // 发送通知
    int rc = ble_gatts_notify_custom(conn_handle, s_rx_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send notification; rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint16_t ble_nus_service_get_rx_handle(void)
{
    return s_rx_chr_val_handle;
}
