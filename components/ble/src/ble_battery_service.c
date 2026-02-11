/**
 * @file ble_battery_service.c
 * @brief Battery Service 实现 (原生 NimBLE 版本)
 * 
 * 标准 BLE Battery Service (UUID: 0x180F)
 */

#include "ble_battery_service.h"
#include "ble_config.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_battery";

/* ================== UUID 定义 ================== */

// Battery Service UUID: 0x180F
static const ble_uuid16_t battery_svc_uuid = BLE_UUID16_INIT(BATTERY_SERVICE_UUID);

// Battery Level Characteristic UUID: 0x2A19
static const ble_uuid16_t battery_level_chr_uuid = BLE_UUID16_INIT(BATTERY_LEVEL_UUID);

/* ================== 私有状态 ================== */
static uint16_t s_level_chr_val_handle;  // Battery Level 特征值句柄
static uint8_t s_battery_level = 100;
static bool s_notify_enabled = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* ================== GATT 访问回调 ================== */

static int battery_level_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 返回当前电池电量
        rc = os_mbuf_append(ctxt->om, &s_battery_level, sizeof(s_battery_level));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        ESP_LOGD(TAG, "Battery level read: %d%%", s_battery_level);
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* ================== GATT 服务定义 ================== */

static const struct ble_gatt_svc_def battery_svc_def[] = {
    {
        // Battery Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &battery_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Battery Level Characteristic
                .uuid = &battery_level_chr_uuid.u,
                .access_cb = battery_level_chr_access,
                .val_handle = &s_level_chr_val_handle,
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

int ble_battery_service_init(void)
{
    int rc;

    s_battery_level = 100;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    // 注册 GATT 服务
    rc = ble_gatts_count_cfg(battery_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count Battery service config; rc=%d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(battery_svc_def);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add Battery service; rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Battery service initialized (Native NimBLE)");
    return 0;
}

void ble_battery_service_deinit(void)
{
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGI(TAG, "Battery service deinitialized");
}

void ble_battery_service_update_level(uint8_t level)
{
    if (level > 100) level = 100;

    if (s_battery_level != level) {
        s_battery_level = level;
        ESP_LOGI(TAG, "Battery level updated: %d%%", s_battery_level);

        // 如果已连接且通知已启用，发送通知
        if (s_level_chr_val_handle != 0) {
            // 通知所有已订阅的连接
            ble_gatts_chr_updated(s_level_chr_val_handle);
        }

        if (s_battery_level <= 20) {
            ESP_LOGW(TAG, "Low battery warning: %d%%", s_battery_level);
        }
    }
}

void ble_battery_service_set_conn_handle(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
}

uint16_t ble_battery_service_get_level_handle(void)
{
    return s_level_chr_val_handle;
}
