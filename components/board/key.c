#include "board.h"
#include "board_private.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ================== 按键相关实现 ================== */
#define BUTTON_COUNT 4
#define DEBOUNCE_TIME_MS 50

typedef struct {
  bool is_pressed;
  uint32_t press_time;
  bool debounced;
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
    if (current_time - state->press_time < DEBOUNCE_TIME_MS) {
      return false;
    }

    state->is_pressed = raw_state;
    state->press_time = current_time;

    // 只有在按下（raw_state == true）且状态稳定变化时才触发
    if (state->is_pressed) {
      return true;
    }
  }

  return false;
}

void board_key_init(void) {
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
    gpio_config(&key_config);

    // 按键状态初始化
    for (int i = 0; i < BUTTON_COUNT; i++) {
        s_button_states[i].is_pressed = false;
        s_button_states[i].press_time = 0;
        s_button_states[i].debounced = false;
    }
    
    ESP_LOGI(BOARD_TAG, "Keys initialized");
}

/* ================== 输入接口实现 ================== */
board_key_t board_key_poll(void) {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (button_debounce(i)) {
      switch (i) {
      case 0:
        return BOARD_KEY_UP;
      case 1:
        return BOARD_KEY_DOWN;
      case 2:
        return BOARD_KEY_ENTER;
      case 3:
        return BOARD_KEY_BACK;
      default:
        break;
      }
    }
  }
  return BOARD_KEY_NONE;
}
