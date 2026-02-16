#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

/* NimBLE 核心库引用 */
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_manager.h"
#include "board.h"

// --- 全局状态 ---
static const char *TAG = "BLE_MANAGER";
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t addr_type;
bool ble_is_connected = false; // 暴露给外部模块
static ble_state_t current_state = BLE_STATE_IDLE;
static ble_message_callback_t message_callback = NULL;

// --- 前向声明 ---
void ble_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

// --- GATT 服务访问回调 ---
static int nus_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t data[len];
    ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL);

    if (len > 0) {
        switch (data[0]) {
            case PROTOCOL_TIME_SYNC: // 0xA1
                if (len >= 5) {
                    // 直接接收时间戳（Unix时间戳，小端格式）
                    uint32_t timestamp = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
                    ESP_LOGI(TAG, "Time Sync: timestamp=%lu", timestamp);
                    
                    // 调用board层的RTC设置接口
                    board_set_rtc_from_timestamp((time_t)timestamp);
                }
                break;
            case PROTOCOL_MSG_FORWARD: // 0xA2
                ESP_LOGI(TAG, "Msg: %.*s", len - 1, &data[1]);
                // 调用消息回调函数
                if (message_callback != NULL && len > 1) {
                    // 简单处理：将消息内容作为字符串传递
                    char* message = (char*)malloc(len);
                    if (message != NULL) {
                        memcpy(message, &data[1], len - 1);
                        message[len - 1] = '\0';
                        message_callback("BLE", message);
                        free(message);
                    }
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown Header: 0x%02X", data[0]);
        }
    }
    return 0;
}

// --- 服务定义 ---
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e),
        .characteristics = (struct ble_gatt_chr_def[]){{
            .uuid = BLE_UUID128_DECLARE(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e),
            .access_cb = nus_chr_access,
            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {0}}
    },
    {0}
};

// --- GAP 事件处理 ---
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_gap_conn_desc desc;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected");
                conn_handle = event->connect.conn_handle;
                ble_is_connected = true;
                current_state = BLE_STATE_CONNECTED;
            } else {
                ble_is_connected = false;
                current_state = BLE_STATE_ADVERTISING;
                ble_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_is_connected = false;
            current_state = BLE_STATE_ADVERTISING;
            ble_advertise();
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
                ESP_LOGW(TAG, "Old bond cleared for repeat pairing");
            }
            return 0;
        }

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU: %d", event->mtu.value);
            return 0;
    }
    return 0;
}

// --- 广播逻辑 ---
void ble_advertise(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};

    // 1. 基础广告包
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"Toy_Device";
    fields.name_len = strlen((char *)fields.name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // 2. 扫描响应包 (放置 128bit UUID 以防广告包空间不足)
    static ble_uuid128_t nus_uuid = BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
    rsp_fields.uuids128 = &nus_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// --- 外部接口实现 ---

void ble_manager_force_reset_bonds(void) {
    ESP_LOGW(TAG, "Resetting all bonds...");
    ble_store_clear(); 
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void ble_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &addr_type);

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;

    current_state = BLE_STATE_ADVERTISING;
    ble_advertise();
}

void ble_stack_task(void *param) {
    nimble_port_run();
}

void ble_manager_start(void) {
    // NimBLE 初始化
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_stack_task);
}

// --- 新实现的接口函数 ---

esp_err_t ble_manager_init(void) {
    ESP_LOGI(TAG, "初始化 BLE 管理器...");
    
    // 初始化状态
    current_state = BLE_STATE_IDLE;
    message_callback = NULL;
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_is_connected = false;
    addr_type = 0;
    
    // 这里可以添加更多的初始化逻辑
    // 例如：检查硬件、初始化NV存储等
    
    ESP_LOGI(TAG, "BLE 管理器初始化完成");
    return ESP_OK;
}

void ble_manager_set_message_callback(ble_message_callback_t callback) {
    message_callback = callback;
    ESP_LOGI(TAG, "消息回调函数已设置");
}

bool ble_manager_is_connected(void) {
    return ble_is_connected;
}

ble_state_t ble_manager_get_state(void) {
    return current_state;
}

esp_err_t ble_manager_start_advertising(void) {
    if (current_state == BLE_STATE_ADVERTISING) {
        ESP_LOGW(TAG, "已经在广播状态");
        return ESP_OK;
    }
    
    if (ble_is_connected) {
        ESP_LOGW(TAG, "已连接，无法开始广播");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "开始 BLE 广播");
    ble_advertise();
    current_state = BLE_STATE_ADVERTISING;
    return ESP_OK;
}

void ble_manager_cleanup(void) {
    ESP_LOGI(TAG, "清理 BLE 管理器资源...");
    
    // 断开当前连接
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    
    // 停止广播
    ble_gap_adv_stop();
    
    // 清理状态
    ble_is_connected = false;
    current_state = BLE_STATE_IDLE;
    message_callback = NULL;
    
    ESP_LOGI(TAG, "BLE 管理器资源清理完成");
}