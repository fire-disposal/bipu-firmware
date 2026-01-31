#include "board.h"
#include "board_private.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ================== 按键相关实现 ================== */
#define BUTTON_COUNT 4
#define DEBOUNCE_TIME_MS 50
#define REPEAT_DELAY_MS 200  // 首次重复延迟
#define REPEAT_RATE_MS 100   // 连续重复间隔

typedef struct {
  bool is_pressed;
  uint32_t last_change_time;
  bool debounced;
  uint32_t press_start_time;
  bool repeat_enabled;
  uint32_t last_repeat_time;
} button_state_t;

static button_state_t s_button_states[BUTTON_COUNT];

/* 读取按键GPIO电平 */
static bool read_button_gpio(int button) {
  gpio_num_t gpio_num;
  switch (button) {
  case 0:
    gpio_num = BOARD_GPIO_KEY_UP;
    break;
  case 1:
    gpio_num = BOARD_GPIO_KEY_DOWN;
    break;
  case 2:
    gpio_num = BOARD_GPIO_KEY_ENTER;
    break;
  case 3:
    gpio_num = BOARD_GPIO_KEY_BACK;
    break;
  default:
    return false;
  }
  // 按键按下时接GND，所以读取到0表示按下
  return (gpio_get_level(gpio_num) == 0);
}

/* 按键去抖动处理 */
static bool button_debounce(int button) {
  button_state_t *state = &s_button_states[button];
  uint32_t current_time = board_time_ms();
  bool raw_state = read_button_gpio(button);

  // 状态发生变化
  if (raw_state != state->is_pressed) {
    // 如果距离上次状态变化时间太短，忽略（去抖）
    if (current_time - state->last_change_time < DEBOUNCE_TIME_MS) {
      return false;
    }

    state->is_pressed = raw_state;
    state->last_change_time = current_time;

    if (state->is_pressed) {
      // 按键按下事件
      state->press_start_time = current_time;
      state->repeat_enabled = true;
      state->last_repeat_time = current_time;
      ESP_LOGD(BOARD_TAG, "Button %d debounced press detected", button);
      return true;
    } else {
      // 按键释放事件
      state->repeat_enabled = false;
      ESP_LOGD(BOARD_TAG, "Button %d released", button);
    }
  } else if (state->is_pressed && state->repeat_enabled) {
    // 处理连续按键
    uint32_t press_duration = current_time - state->press_start_time;
    if (press_duration > REPEAT_DELAY_MS) {
      if (current_time - state->last_repeat_time >= REPEAT_RATE_MS) {
        state->last_repeat_time = current_time;
        ESP_LOGD(BOARD_TAG, "Button %d repeat event", button);
        return true;
      }
    }
  }

  return false;
}

void board_key_init(void) {
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
    } else {
        ESP_LOGI(BOARD_TAG, "GPIO config successful");
    }

    // 按键状态初始化
    for (int i = 0; i < BUTTON_COUNT; i++) {
        s_button_states[i].is_pressed = false;
        s_button_states[i].last_change_time = 0;
        s_button_states[i].debounced = false;
        s_button_states[i].press_start_time = 0;
        s_button_states[i].repeat_enabled = false;
        s_button_states[i].last_repeat_time = 0;
    }
    
    // 读取初始GPIO状态
    for (int i = 0; i < BUTTON_COUNT; i++) {
        bool raw_state = read_button_gpio(i);
        ESP_LOGI(BOARD_TAG, "Button %d initial state: %d (0=pressed, 1=released)", i, raw_state);
    }
    
    ESP_LOGI(BOARD_TAG, "Keys initialized");
}

/* ================== 输入接口实现 ================== */
board_key_t board_key_poll(void) {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    bool debounced = button_debounce(i);
    if (debounced) {
      ESP_LOGD(BOARD_TAG, "Button %d pressed, raw state: %d", i, read_button_gpio(i));
      switch (i) {
      case 0:
        ESP_LOGD(BOARD_TAG, "UP key detected");
        return BOARD_KEY_UP;
      case 1:
        ESP_LOGD(BOARD_TAG, "DOWN key detected");
        return BOARD_KEY_DOWN;
      case 2:
        ESP_LOGD(BOARD_TAG, "ENTER key detected");
        return BOARD_KEY_ENTER;
      case 3:
        ESP_LOGD(BOARD_TAG, "BACK key detected");
        return BOARD_KEY_BACK;
      default:
        break;
      }
    }
  }
  return BOARD_KEY_NONE;
}
