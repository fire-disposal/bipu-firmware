#include "app_conn_sm.h"
#include "board.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG = "app_conn_sm";

typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTED_INIT,
    CONN_STATE_CONNECTED_STABLE,
} conn_state_e;

typedef struct {
    conn_state_e state;
    uint32_t connection_start_time;
    uint32_t last_blink_time;
    bool led_state;
} conn_sm_t;

static conn_sm_t s_conn_sm = { .state = CONN_STATE_DISCONNECTED };

void app_conn_sm_tick(bool is_connected)
{
    // 如果正在播放消息光效，上层会检查并跳过调用此函数
    uint32_t now = board_time_ms();

    switch (s_conn_sm.state) {
        case CONN_STATE_DISCONNECTED:
            if (is_connected) {
                s_conn_sm.state = CONN_STATE_CONNECTED_INIT;
                s_conn_sm.connection_start_time = now;
                s_conn_sm.last_blink_time = 0;
                s_conn_sm.led_state = false;
                ESP_LOGI(TAG, "BLE已连接，进入初始化闪烁阶段");
            }
            break;

        case CONN_STATE_CONNECTED_INIT:
            if (!is_connected) {
                s_conn_sm.state = CONN_STATE_DISCONNECTED;
                board_leds_off();
                ESP_LOGI(TAG, "连接在闪烁阶段中断，回到断开状态");
                break;
            }

            if (now - s_conn_sm.connection_start_time < 3000U) {
                if (now - s_conn_sm.last_blink_time > 200U) {
                    if (s_conn_sm.led_state) {
                            board_leds_set((board_leds_t){ .led1 = 0, .led2 = 0, .led3 = 255 });
                        } else {
                            board_leds_off();
                        }
                    s_conn_sm.led_state = !s_conn_sm.led_state;
                    s_conn_sm.last_blink_time = now;
                }
            } else {
                s_conn_sm.state = CONN_STATE_CONNECTED_STABLE;
                ESP_LOGI(TAG, "连接稳定，进入常驻检查阶段");
            }
            break;

        case CONN_STATE_CONNECTED_STABLE:
            if (!is_connected) {
                s_conn_sm.state = CONN_STATE_DISCONNECTED;
                board_leds_off();
                ESP_LOGI(TAG, "BLE已断开连接");
                break;
            }

            // 连接稳定：不管理时间同步状态，也不额外操作 LED
            board_leds_off();
            break;
    }
}
