#include "board.h"
#include "board_hal.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ================== 三个独立白光 LED 接口实现 ================== */
void board_leds_init(void) {
  // 在上电后尽快接管可能为上拉的引脚（例如 LED_2 为 Strapping Pin）
  gpio_reset_pin(BOARD_GPIO_LED_1);
  gpio_reset_pin(BOARD_GPIO_LED_2);
  gpio_reset_pin(BOARD_GPIO_LED_3);

  // 配置 LED GPIO 为输出
  gpio_config_t led_config = {
      .pin_bit_mask = (1ULL << BOARD_GPIO_LED_1) | (1ULL << BOARD_GPIO_LED_2) |
                      (1ULL << BOARD_GPIO_LED_3),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&led_config);

  // 立即熄灭以覆盖上电的默认电平（尤其是 GPIO2）
  board_leds_off();
  ESP_LOGI(BOARD_TAG, "LEDs initialized (GPIO)");
}

void board_leds_set(board_leds_t leds) {
  // 使用阈值控制三路白光灯（>127 视为点亮）
  gpio_set_level(BOARD_GPIO_LED_1, leds.led1 > 127 ? 1 : 0);
  gpio_set_level(BOARD_GPIO_LED_2, leds.led2 > 127 ? 1 : 0);
  gpio_set_level(BOARD_GPIO_LED_3, leds.led3 > 127 ? 1 : 0);
}

void board_leds_off(void) {
  gpio_set_level(BOARD_GPIO_LED_1, 0);
  gpio_set_level(BOARD_GPIO_LED_2, 0);
  gpio_set_level(BOARD_GPIO_LED_3, 0);
}
