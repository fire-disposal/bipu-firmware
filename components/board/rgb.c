#include "board.h"
#include "board_private.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ================== RGB灯接口实现 ================== */
void board_rgb_init(void) {
  // 配置RGB灯GPIO为输出
  gpio_config_t rgb_config = {
      .pin_bit_mask = (1ULL << BOARD_GPIO_RGB_R) | (1ULL << BOARD_GPIO_RGB_G) |
                      (1ULL << BOARD_GPIO_RGB_B),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&rgb_config);
  
  board_rgb_off();
  ESP_LOGI(BOARD_TAG, "RGB LED initialized (GPIO)");
}

void board_rgb_set(board_rgb_t color) {
  // 简单的阈值判断，模拟 PWM 到 GPIO 高低电平
  // 大于 127 视为高电平（亮），否则为低电平（灭）
  gpio_set_level(BOARD_GPIO_RGB_R, color.r > 127 ? 1 : 0);
  gpio_set_level(BOARD_GPIO_RGB_G, color.g > 127 ? 1 : 0);
  gpio_set_level(BOARD_GPIO_RGB_B, color.b > 127 ? 1 : 0);
}

void board_rgb_off(void) {
  gpio_set_level(BOARD_GPIO_RGB_R, 0);
  gpio_set_level(BOARD_GPIO_RGB_G, 0);
  gpio_set_level(BOARD_GPIO_RGB_B, 0);
}
