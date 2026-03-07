/**
 * @file app_ble.c
 * @brief 蓝牙应用层业务回调 (Bipupu 协议版本 1.2)
 *
 * ── 任务解耦架构 ─────────────────────────────────────────────────────────
 * NimBLE 协议栈运行在独立的 FreeRTOS 任务中。若在 NimBLE 任务回调里直接
 * 调用 ui_show_message_with_timestamp()，会导致：
 *   1. NVS 写入（~10-50ms）阻塞 BLE 协议栈，可能触发连接超时断开
 *   2. board_vibrate / board_leds 等硬件驱动从非预期任务上下文调用
 *   3. UI 互斥锁被长时间持有，阻塞 ui_tick / ui_on_key 最多 200ms
 *
 * 解决方案：
 *   ble_message_received()  → xQueueSend（非阻塞，仅 ~1µs）→ 立即返回
 *   app_ble_process_pending() → xQueueReceive → ui_show_message_*（在 app_task 中）
 *
 * ── 协议说明 ─────────────────────────────────────────────────────────────
 *   发送者/正文已在 bipupu_protocol.c 中从二进制字段解析，无字符串拼接：
 *     网络转发消息：sender = 联系人显示名，message = 消息正文
 *     直接蓝牙测试：sender = "App"，message = 消息正文
 */

#include "app_ble.h"
#include "board.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char* TAG = "app_ble";

/* ── 消息事件队列 ──────────────────────────────────────────────────────── */

/**
 * 单条消息事件结构体
 * 字段宽度与 storage_message_t / ui_message_t 保持一致，
 * 避免在队列和 UI 层之间引入额外截断。
 */
typedef struct {
    char     sender[32];   /**< 与 ui_message_t.sender 同宽 */
    char     body[128];    /**< 与 ui_message_t.text 同宽   */
    uint32_t timestamp;
} ble_msg_event_t;

/** 队列深度：缓冲最多 8 条消息（内存开销约 1.3 KB） */
#define MSG_QUEUE_DEPTH  8

static QueueHandle_t s_msg_queue = NULL;

/* ── 队列初始化 ────────────────────────────────────────────────────────── */

void app_ble_init(void)
{
    if (s_msg_queue != NULL) return; /* 幂等 */
    s_msg_queue = xQueueCreate(MSG_QUEUE_DEPTH, sizeof(ble_msg_event_t));
    if (s_msg_queue == NULL) {
        /* 队列创建失败是严重错误，但不 panic，降级为消息丢弃模式 */
        ESP_LOGE(TAG, "消息队列创建失败！BLE 消息将全部被丢弃");
    } else {
        ESP_LOGI(TAG, "消息事件队列已创建 (深度=%d, 元素=%dB)",
                 MSG_QUEUE_DEPTH, (int)sizeof(ble_msg_event_t));
    }
}

/* ── 队列消费（在 app_task 中调用）──────────────────────────────────────── */

void app_ble_process_pending(void)
{
    if (s_msg_queue == NULL) return;

    ble_msg_event_t evt;
    /* 排空队列中的所有待处理消息 */
    while (xQueueReceive(s_msg_queue, &evt, 0) == pdTRUE) {
        ESP_LOGI(TAG, "处理BLE消息 [%s]: %.40s... (ts=%u)",
                 evt.sender, evt.body, evt.timestamp);
        /*
         * ui_show_message_with_timestamp 在此处执行：
         *   1. UI 状态更新（持锁，快速内存操作）
         *   2. NVS 持久化（释放锁后，慢速但不阻塞 UI）
         *   3. 硬件通知（board_notify / board_leds / board_vibrate）
         */
        ui_show_message_with_timestamp(evt.sender, evt.body, evt.timestamp);
    }
}

/* ── NimBLE 任务回调实现 ─────────────────────────────────────────────────── */

/**
 * @brief 蓝牙文本消息接收回调（在 NimBLE 任务上下文中调用）
 *
 * 仅执行非阻塞的队列投递，立即返回，避免阻塞 BLE 协议栈。
 */
void ble_message_received(const char* sender, const char* message, uint32_t timestamp)
{
    if (!message || message[0] == '\0') {
        ESP_LOGW(TAG, "收到空消息，已忽略");
        return;
    }

    if (s_msg_queue == NULL) {
        ESP_LOGW(TAG, "消息队列未初始化，消息丢弃（请先调用 app_ble_init）");
        return;
    }

    ble_msg_event_t evt;
    strncpy(evt.sender, sender ? sender : "App", sizeof(evt.sender) - 1);
    evt.sender[sizeof(evt.sender) - 1] = '\0';
    strncpy(evt.body, message, sizeof(evt.body) - 1);
    evt.body[sizeof(evt.body) - 1] = '\0';
    evt.timestamp = timestamp;

    /* 非阻塞投递：队列满时丢弃最新消息（优于阻塞 NimBLE 任务） */
    if (xQueueSend(s_msg_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "消息队列已满，消息丢弃: [%s]", evt.sender);
    } else {
        ESP_LOGD(TAG, "消息已入队: [%s]", evt.sender);
    }
}

/**
 * @brief 蓝牙连接状态变化回调（在 NimBLE 任务上下文中调用）
 *
 * 仅调用非阻塞的 LED 状态机和 UI 重绘请求，对 NimBLE 任务安全。
 */
void ble_connection_changed(bool connected)
{
    ESP_LOGI(TAG, "BLE %s", connected ? "connected" : "disconnected");

    if (connected) {
        /* 连接建立：停止广播跑马灯，改为"已连接"双闪 */
        board_leds_set_mode(BOARD_LED_MODE_CONNECTED);
    } else {
        /* 连接断开：恢复广播跑马灯，提示设备可被搜索 */
        board_leds_set_mode(BOARD_LED_MODE_ADVERTISING);
    }

    /* 请求 UI 重绘状态栏（仅设置标志位 + xTaskNotifyGive，非阻塞） */
    ui_request_redraw();
}
