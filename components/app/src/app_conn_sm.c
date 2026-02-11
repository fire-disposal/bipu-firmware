#include "app_conn_sm.h"
#include "board.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_conn_sm";

/* ========== 配置常量 ========== */
#define CONNECT_BLINK_DURATION_MS  3000U  // 连接闪烁持续时间
#define CONNECT_BLINK_INTERVAL_MS  200U   // 闪烁间隔

typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTED_BLINK,   // 连接后闪烁阶段
    CONN_STATE_CONNECTED_STABLE,  // 连接稳定
} conn_state_e;

typedef struct {
    conn_state_e state;
    uint32_t state_enter_time;
    uint32_t last_blink_time;
    bool led_on;
} conn_sm_t;

static conn_sm_t s_sm = {0};

void app_conn_sm_tick(bool is_connected)
{
    uint32_t now = board_time_ms();

    switch (s_sm.state) {
        case CONN_STATE_DISCONNECTED:
            if (is_connected) {
                s_sm.state = CONN_STATE_CONNECTED_BLINK;
                s_sm.state_enter_time = now;
                s_sm.last_blink_time = now;
                s_sm.led_on = true;
                board_leds_set((board_leds_t){ .led1 = 0, .led2 = 0, .led3 = 255 });
                ESP_LOGI(TAG, "BLE 已连接");
            }
            break;

        case CONN_STATE_CONNECTED_BLINK:
            if (!is_connected) {
                s_sm.state = CONN_STATE_DISCONNECTED;
                board_leds_off();
                ESP_LOGI(TAG, "BLE 已断开");
                break;
            }

            // 闪烁期间
            if (now - s_sm.state_enter_time < CONNECT_BLINK_DURATION_MS) {
                if (now - s_sm.last_blink_time >= CONNECT_BLINK_INTERVAL_MS) {
                    s_sm.led_on = !s_sm.led_on;
                    s_sm.last_blink_time = now;
                    if (s_sm.led_on) {
                        board_leds_set((board_leds_t){ .led1 = 0, .led2 = 0, .led3 = 255 });
                    } else {
                        board_leds_off();
                    }
                }
            } else {
                // 闪烁结束，进入稳定状态
                s_sm.state = CONN_STATE_CONNECTED_STABLE;
                board_leds_off();
                ESP_LOGI(TAG, "连接稳定");
            }
            break;

        case CONN_STATE_CONNECTED_STABLE:
            if (!is_connected) {
                s_sm.state = CONN_STATE_DISCONNECTED;
                ESP_LOGI(TAG, "BLE 已断开");
            }
            // 稳定状态不操作 LED
            break;
    }
}
