#include "board_pins.h"   // GPIO引脚定义
#include "board.h"        // 公共接口
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

/* ================== 按键相关实现 ================== */
#define BUTTON_COUNT 4
#define DEBOUNCE_TIME_MS 50
#define REPEAT_DELAY_MS 500   // 首次重复延迟 (增加以区分长按)
#define REPEAT_RATE_MS 150    // 连续重复间隔
#define LONG_PRESS_MS 800     // 长按阈值

// 按键事件类型
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT_PRESS,
    KEY_EVENT_LONG_PRESS,
    KEY_EVENT_REPEAT,
} key_event_type_t;

typedef struct {
    bool is_pressed;           // 当前按下状态
    bool was_pressed;          // 上一次状态 (用于边沿检测)
    uint32_t last_change_time; // 上次状态变化时间
    uint32_t press_start_time; // 按下开始时间
    bool long_press_fired;     // 长按事件是否已触发
    bool repeat_enabled;       // 是否启用重复
    uint32_t last_repeat_time; // 上次重复时间
    uint8_t debounce_counter;  // 去抖计数器
} button_state_t;

static button_state_t s_button_states[BUTTON_COUNT];
static bool s_keys_initialized = false;

// GPIO映射表 (避免switch开销)
static const gpio_num_t s_button_gpios[BUTTON_COUNT] = {
    BOARD_GPIO_KEY_UP,
    BOARD_GPIO_KEY_DOWN,
    BOARD_GPIO_KEY_ENTER,
    BOARD_GPIO_KEY_BACK
};

/* 安全读取按键GPIO电平 */
static bool read_button_gpio(int button) {
    if (button < 0 || button >= BUTTON_COUNT) {
        return false;
    }
    // 按键按下时接GND，所以读取到0表示按下
    return (gpio_get_level(s_button_gpios[button]) == 0);
}

/* 按键去抖动处理 - 返回事件类型 */
static key_event_type_t button_process(int button) {
    if (button < 0 || button >= BUTTON_COUNT) {
        return KEY_EVENT_NONE;
    }

    button_state_t *state = &s_button_states[button];
    uint32_t current_time = board_time_ms();
    bool raw_state = read_button_gpio(button);
    key_event_type_t event = KEY_EVENT_NONE;

    // 软件去抖: 状态需要稳定一段时间才被接受
    if (raw_state != state->is_pressed) {
        if (current_time - state->last_change_time >= DEBOUNCE_TIME_MS) {
            state->was_pressed = state->is_pressed;
            state->is_pressed = raw_state;
            state->last_change_time = current_time;

            if (state->is_pressed && !state->was_pressed) {
                // 上升沿: 按键按下
                state->press_start_time = current_time;
                state->long_press_fired = false;
                state->repeat_enabled = false;
                state->last_repeat_time = current_time;
                ESP_LOGD(BOARD_TAG, "Button %d pressed", button);
                // 不立即返回事件，等待判断是短按还是长按
            } else if (!state->is_pressed && state->was_pressed) {
                // 下降沿: 按键释放
                uint32_t press_duration = current_time - state->press_start_time;
                
                // 只有在未触发长按时才返回短按事件
                if (!state->long_press_fired && press_duration < LONG_PRESS_MS) {
                    event = KEY_EVENT_SHORT_PRESS;
                    ESP_LOGD(BOARD_TAG, "Button %d short press (%lu ms)", button, press_duration);
                }
                state->repeat_enabled = false;
                state->long_press_fired = false;
            }
        }
    } else {
        // 状态未变化时更新时间戳
        state->last_change_time = current_time;
    }

    // 长按检测 (按住时检测)
    if (state->is_pressed && !state->long_press_fired) {
        uint32_t press_duration = current_time - state->press_start_time;
        if (press_duration >= LONG_PRESS_MS) {
            state->long_press_fired = true;
            state->repeat_enabled = true;
            state->last_repeat_time = current_time;
            event = KEY_EVENT_LONG_PRESS;
            ESP_LOGD(BOARD_TAG, "Button %d long press", button);
        }
    }

    // 重复按键检测 (长按后持续按住)
    if (state->is_pressed && state->repeat_enabled && state->long_press_fired) {
        if (current_time - state->last_repeat_time >= REPEAT_RATE_MS) {
            state->last_repeat_time = current_time;
            event = KEY_EVENT_REPEAT;
            ESP_LOGD(BOARD_TAG, "Button %d repeat", button);
        }
    }

    return event;
}

void board_key_init(void) {
    if (s_keys_initialized) {
        ESP_LOGW(BOARD_TAG, "Keys already initialized");
        return;
    }

    ESP_LOGI(BOARD_TAG, "Initializing keys with GPIOs: UP=%d, DOWN=%d, ENTER=%d, BACK=%d",
             BOARD_GPIO_KEY_UP, BOARD_GPIO_KEY_DOWN, BOARD_GPIO_KEY_ENTER, BOARD_GPIO_KEY_BACK);
    
    // 配置按键GPIO为输入，上拉模式（按钮按下时接GND）
    gpio_config_t key_config = {
        .pin_bit_mask =
            (1ULL << BOARD_GPIO_KEY_UP) | (1ULL << BOARD_GPIO_KEY_DOWN) |
            (1ULL << BOARD_GPIO_KEY_ENTER) | (1ULL << BOARD_GPIO_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&key_config);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(BOARD_TAG, "GPIO config successful");

    // 按键状态初始化 (使用memset更安全)
    memset(s_button_states, 0, sizeof(s_button_states));
    uint32_t init_time = board_time_ms();
    
    for (int i = 0; i < BUTTON_COUNT; i++) {
        s_button_states[i].last_change_time = init_time;
        // 读取初始GPIO状态
        bool raw_state = read_button_gpio(i);
        s_button_states[i].is_pressed = raw_state;
        s_button_states[i].was_pressed = raw_state;
        ESP_LOGI(BOARD_TAG, "Button %d initial state: %s", i, raw_state ? "pressed" : "released");
    }
    
    s_keys_initialized = true;
    ESP_LOGI(BOARD_TAG, "Keys initialized successfully");
}

/* ================== 输入接口实现 ================== */
board_key_t board_key_poll(void) {
    if (!s_keys_initialized) {
        return BOARD_KEY_NONE;
    }

    // 轮询所有按键，返回第一个检测到的事件
    for (int i = 0; i < BUTTON_COUNT; i++) {
        key_event_type_t event = button_process(i);
        
        // 只返回短按和长按事件，重复事件用于特殊处理
        if (event == KEY_EVENT_SHORT_PRESS || event == KEY_EVENT_LONG_PRESS) {
            static const board_key_t key_map[BUTTON_COUNT] = {
                BOARD_KEY_UP,
                BOARD_KEY_DOWN,
                BOARD_KEY_ENTER,
                BOARD_KEY_BACK
            };
            
            ESP_LOGD(BOARD_TAG, "Key %d detected (event=%d)", i, event);
            return key_map[i];
        }
    }
    return BOARD_KEY_NONE;
}

/* 检查指定按键是否当前被按下 */
bool board_key_is_pressed(board_key_t key) {
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

/* 检查指定按键是否正在长按 */
bool board_key_is_long_pressed(board_key_t key) {
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

/* 获取按键按下持续时间 (毫秒) */
uint32_t board_key_press_duration(board_key_t key) {
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
