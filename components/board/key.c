/**
 * @file key.c
 * @brief 按键驱动 - GPIO 边沿中断模式
 *
 * 中断响应延迟：<5ms
 * 去抖动：软件定时器 50ms
 */

#include "board_pins.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

#define BUTTON_COUNT 4
#define DEBOUNCE_TIME_MS 50
#define REPEAT_DELAY_MS 500
#define REPEAT_RATE_MS 150
#define LONG_PRESS_MS 800

/* 按键事件类型 */
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT_PRESS,
    KEY_EVENT_LONG_PRESS,
    KEY_EVENT_REPEAT,
} key_event_type_t;

typedef struct {
    int button_idx;
    key_event_type_t event;
} key_isr_event_t;

typedef struct {
    bool is_pressed;
    uint32_t press_start_time;
    bool long_press_fired;
    bool repeat_enabled;
    uint32_t last_repeat_time;
    bool debounce_active;
    uint32_t debounce_start_time;
} button_state_t;

static button_state_t s_button_states[BUTTON_COUNT];
static bool s_keys_initialized = false;
static QueueHandle_t s_key_queue = NULL;
static esp_timer_handle_t s_repeat_timer = NULL;

static const gpio_num_t s_button_gpios[BUTTON_COUNT] = {
    BOARD_GPIO_KEY_UP,
    BOARD_GPIO_KEY_DOWN,
    BOARD_GPIO_KEY_ENTER,
    BOARD_GPIO_KEY_BACK
};

static const board_key_t s_short_map[BUTTON_COUNT] = {
    BOARD_KEY_UP, BOARD_KEY_DOWN, BOARD_KEY_ENTER, BOARD_KEY_BACK
};

static const board_key_t s_long_map[BUTTON_COUNT] = {
    BOARD_KEY_UP_LONG, BOARD_KEY_DOWN_LONG, BOARD_KEY_ENTER_LONG, BOARD_KEY_BACK_LONG
};

static const board_key_t s_repeat_map[BUTTON_COUNT] = {
    BOARD_KEY_UP_REPEAT, BOARD_KEY_DOWN_REPEAT, BOARD_KEY_ENTER_REPEAT, BOARD_KEY_BACK_REPEAT
};

/* 重复按键定时器回调 */
static void repeat_timer_cb(void* arg)
{
    if (!s_keys_initialized || s_key_queue == NULL) return;

    uint32_t now = board_time_ms();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        button_state_t* state = &s_button_states[i];
        if (state->is_pressed && state->repeat_enabled && state->long_press_fired) {
            if (now - state->last_repeat_time >= REPEAT_RATE_MS) {
                state->last_repeat_time = now;
                board_key_t key = s_repeat_map[i];
                xQueueSendFromISR(s_key_queue, &key, NULL);
            }
        }
    }
}

/* 启动重复定时器 */
static void start_repeat_timer(void)
{
    if (s_repeat_timer != NULL) {
        esp_timer_start_periodic(s_repeat_timer, REPEAT_RATE_MS * 1000);
    }
}

/* 停止重复定时器 */
static void stop_repeat_timer(void)
{
    if (s_repeat_timer != NULL) {
        esp_timer_stop(s_repeat_timer);
    }
}

/* 按键状态处理 */
static void process_button_state(int button, bool is_pressed, uint32_t timestamp)
{
    if (button < 0 || button >= BUTTON_COUNT) return;

    button_state_t* state = &s_button_states[button];
    board_key_t key = BOARD_KEY_NONE;

    if (is_pressed && !state->is_pressed) {
        if (!state->debounce_active) {
            state->debounce_active = true;
            state->debounce_start_time = timestamp;
        } else if (timestamp - state->debounce_start_time >= DEBOUNCE_TIME_MS) {
            state->is_pressed = true;
            state->press_start_time = timestamp;
            state->long_press_fired = false;
            state->repeat_enabled = false;
            state->debounce_active = false;
        }
    } else if (!is_pressed && state->is_pressed) {
        if (!state->debounce_active) {
            state->debounce_active = true;
            state->debounce_start_time = timestamp;
        } else if (timestamp - state->debounce_start_time >= DEBOUNCE_TIME_MS) {
            uint32_t duration = timestamp - state->press_start_time;
            if (!state->long_press_fired && duration < LONG_PRESS_MS) {
                key = s_short_map[button];
            }
            state->is_pressed = false;
            state->repeat_enabled = false;
            state->long_press_fired = false;
            state->debounce_active = false;

            bool any_pressed = false;
            for (int i = 0; i < BUTTON_COUNT; i++) {
                if (s_button_states[i].is_pressed) {
                    any_pressed = true;
                    break;
                }
            }
            if (!any_pressed) {
                stop_repeat_timer();
            }
        }
    }

    /* 长按检测 */
    if (state->is_pressed && !state->long_press_fired) {
        if (timestamp - state->press_start_time >= LONG_PRESS_MS) {
            state->long_press_fired = true;
            state->repeat_enabled = true;
            state->last_repeat_time = timestamp;
            key = s_long_map[button];
            start_repeat_timer();
        }
    }

    if (key != BOARD_KEY_NONE) {
        xQueueSendFromISR(s_key_queue, &key, NULL);
    }
}

/* GPIO 中断服务包装函数 */
static void gpio_isr_wrapper(void* arg)
{
    int button = (int)arg;
    bool level = gpio_get_level(s_button_gpios[button]);
    uint32_t now = board_time_ms();
    process_button_state(button, (level == 0), now);
}

void board_key_init(void)
{
    if (s_keys_initialized) {
        return;
    }

    /* 初始化队列 */
    s_key_queue = xQueueCreate(10, sizeof(board_key_t));
    if (s_key_queue == NULL) {
        return;
    }

    /* 按键状态初始化 */
    memset(s_button_states, 0, sizeof(s_button_states));

    /* 配置 GPIO 中断 */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask =
            (1ULL << BOARD_GPIO_KEY_UP) |
            (1ULL << BOARD_GPIO_KEY_DOWN) |
            (1ULL << BOARD_GPIO_KEY_ENTER) |
            (1ULL << BOARD_GPIO_KEY_BACK),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    /* 读取初始状态 */
    uint32_t init_time = board_time_ms();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        bool raw_state = (gpio_get_level(s_button_gpios[i]) == 0);
        s_button_states[i].is_pressed = raw_state;
        s_button_states[i].press_start_time = init_time;
    }

    /* 创建重复定时器 */
    const esp_timer_create_args_t timer_args = {
        .callback = &repeat_timer_cb,
        .name = "key_repeat_timer"
    };
    esp_timer_create(&timer_args, &s_repeat_timer);

    /* 注册中断处理函数 */
    gpio_install_isr_service(0);
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_isr_handler_add(s_button_gpios[i], gpio_isr_wrapper, (void*)i);
    }

    s_keys_initialized = true;
}

board_key_t board_key_poll(void)
{
    if (!s_keys_initialized || s_key_queue == NULL) {
        return BOARD_KEY_NONE;
    }

    board_key_t key = BOARD_KEY_NONE;
    if (xQueueReceive(s_key_queue, &key, 0) == pdTRUE) {
        return key;
    }
    return BOARD_KEY_NONE;
}

bool board_key_is_pressed(board_key_t key)
{
    if (!s_keys_initialized) return false;

    int idx = -1;
    switch (key) {
        case BOARD_KEY_UP:    idx = 0; break;
        case BOARD_KEY_DOWN:  idx = 1; break;
        case BOARD_KEY_ENTER: idx = 2; break;
        case BOARD_KEY_BACK:  idx = 3; break;
        default: return false;
    }

    return s_button_states[idx].is_pressed;
}

bool board_key_is_long_pressed(board_key_t key)
{
    if (!s_keys_initialized) return false;

    int idx = -1;
    switch (key) {
        case BOARD_KEY_UP:    idx = 0; break;
        case BOARD_KEY_DOWN:  idx = 1; break;
        case BOARD_KEY_ENTER: idx = 2; break;
        case BOARD_KEY_BACK:  idx = 3; break;
        default: return false;
    }

    return s_button_states[idx].is_pressed && s_button_states[idx].long_press_fired;
}

uint32_t board_key_press_duration(board_key_t key)
{
    if (!s_keys_initialized) return 0;

    int idx = -1;
    switch (key) {
        case BOARD_KEY_UP:    idx = 0; break;
        case BOARD_KEY_DOWN:  idx = 1; break;
        case BOARD_KEY_ENTER: idx = 2; break;
        case BOARD_KEY_BACK:  idx = 3; break;
        default: return 0;
    }

    if (!s_button_states[idx].is_pressed) return 0;
    return board_time_ms() - s_button_states[idx].press_start_time;
}
