#include "buttons.h"
#include "driver/gpio.h"

/* ================== GPIO 定义 ================== */

#define KEY_UP_GPIO     GPIO_NUM_10
#define KEY_DOWN_GPIO   GPIO_NUM_11
#define KEY_ENTER_GPIO  GPIO_NUM_12
#define KEY_BACK_GPIO   GPIO_NUM_13

static const gpio_num_t s_button_gpio[BUTTON_COUNT] = {
    [BUTTON_UP]    = KEY_UP_GPIO,
    [BUTTON_DOWN]  = KEY_DOWN_GPIO,
    [BUTTON_ENTER] = KEY_ENTER_GPIO,
    [BUTTON_BACK]  = KEY_BACK_GPIO,
};

/* ================== 实现 ================== */

void buttons_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < BUTTON_COUNT; i++) {
        io_conf.pin_bit_mask |= (1ULL << s_button_gpio[i]);
    }

    gpio_config(&io_conf);
}

bool buttons_is_pressed(button_id_t button)
{
    if (button >= BUTTON_COUNT) {
        return false;
    }

    /* 上拉输入：低电平表示按下 */
    return gpio_get_level(s_button_gpio[button]) == 0;
}
