/**
 * @file key.c
 * @brief 按键驱动 - 轮询模式（10ms周期）
 *
 * 采用软件轮询代替GPIO中断，避免多按钮间的中断优先级竞争
 * 去抖动：50ms
 * 长按检测：800ms
 * 轮询周期：10ms
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
#define LONG_PRESS_MS 800
#define KEY_QUEUE_SIZE 16
#define KEY_POLL_INTERVAL_MS 10  /* 轮询周期 */

typedef struct {
    bool is_pressed;
    uint32_t press_start_time;
    bool long_press_fired;
    bool debounce_active;
    uint32_t debounce_start_time;
} button_state_t;

static button_state_t s_button_states[BUTTON_COUNT];
static bool s_keys_initialized = false;
static QueueHandle_t s_key_queue = NULL;
static SemaphoreHandle_t s_key_mutex = NULL;
static esp_timer_handle_t s_poll_timer = NULL;  /* 轮询定时器 */

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

/* 按键状态处理 - 包含完整的去抖和状态机 */
static void process_button_state(int button, bool is_pressed, uint32_t timestamp)
{
    if (button < 0 || button >= BUTTON_COUNT) return;

    button_state_t* state = &s_button_states[button];
    board_key_t key = BOARD_KEY_NONE;

    if (is_pressed && !state->is_pressed) {
        // 按下过程中的去抖
        if (!state->debounce_active) {
            state->debounce_active = true;
            state->debounce_start_time = timestamp;
        } else if (timestamp - state->debounce_start_time >= DEBOUNCE_TIME_MS) {
            // 去抖完成，确认按下
            state->is_pressed = true;
            state->press_start_time = timestamp;
            state->long_press_fired = false;
            state->debounce_active = false;
        }
    } else if (!is_pressed && state->is_pressed) {
        // 释放过程中的去抖
        if (!state->debounce_active) {
            state->debounce_active = true;
            state->debounce_start_time = timestamp;
        } else if (timestamp - state->debounce_start_time >= DEBOUNCE_TIME_MS) {
            // 去抖完成，确认释放
            uint32_t duration = timestamp - state->press_start_time;
            // 如果还没有触发长按，则是短按
            if (!state->long_press_fired && duration < LONG_PRESS_MS) {
                key = s_short_map[button];
            }
            state->is_pressed = false;
            state->long_press_fired = false;
            state->debounce_active = false;
        }
    }

    /* 长按检测 - 仅触发一次 */
    if (state->is_pressed && !state->long_press_fired) {
        if (timestamp - state->press_start_time >= LONG_PRESS_MS) {
            state->long_press_fired = true;
            key = s_long_map[button];
        }
    }

    if (key != BOARD_KEY_NONE) {
        xQueueSendFromISR(s_key_queue, &key, NULL);
    }
}

/* 轮询定时器回调 - 扫描所有按钮状态 */
static void key_poll_timer_callback(void* arg)
{
    uint32_t now = board_time_ms();
    
    /* 逐个扫描所有按钮（不会相互干扰） */
    for (int i = 0; i < BUTTON_COUNT; i++) {
        bool raw_state = (gpio_get_level(s_button_gpios[i]) == 0);  /* 低电平为按下 */
        process_button_state(i, raw_state, now);
    }
}

void board_key_init(void)
{
    if (s_keys_initialized) {
        return;
    }

    /* 创建互斥锁用于线程安全 */
    s_key_mutex = xSemaphoreCreateMutex();
    if (s_key_mutex == NULL) {
        return;
    }

    /* 初始化队列 */
    s_key_queue = xQueueCreate(KEY_QUEUE_SIZE, sizeof(board_key_t));
    if (s_key_queue == NULL) {
        vSemaphoreDelete(s_key_mutex);
        return;
    }

    /* 按键状态初始化 */
    memset(s_button_states, 0, sizeof(s_button_states));

    /* 配置 GPIO（仅输入，无中断） */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,  /* 关闭中断，使用轮询 */
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

    /* 创建周期定时器进行轮询 */
    const esp_timer_create_args_t timer_args = {
        .callback = key_poll_timer_callback,
        .arg = NULL,
        .name = "key_poll",
        .dispatch_method = ESP_TIMER_TASK,  /* 在 timer task 中执行 */
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &s_poll_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to create key poll timer: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_key_mutex);
        vQueueDelete(s_key_queue);
        return;
    }

    /* 启动定时器 - 10ms 周期 */
    ret = esp_timer_start_periodic(s_poll_timer, KEY_POLL_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to start key poll timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_poll_timer);
        vSemaphoreDelete(s_key_mutex);
        vQueueDelete(s_key_queue);
        return;
    }

    s_keys_initialized = true;
    ESP_LOGI(BOARD_TAG, "Key input initialized - polling mode (interval=%dms, debounce=%dms, long_press=%dms)", 
             KEY_POLL_INTERVAL_MS, DEBOUNCE_TIME_MS, LONG_PRESS_MS);
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
