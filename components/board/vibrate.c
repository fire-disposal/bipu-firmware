#include "board.h"
#include "board_private.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

/* ================== 震动接口实现 ================== */
// PWM配置参数
#define VIBRATE_LEDC_TIMER LEDC_TIMER_0
#define VIBRATE_LEDC_MODE LEDC_LOW_SPEED_MODE
#define VIBRATE_LEDC_CHANNEL LEDC_CHANNEL_0
#define VIBRATE_LEDC_RES LEDC_TIMER_10_BIT
#define VIBRATE_LEDC_FREQ 200  // 降低频率以获得更强的震感
#define VIBRATE_DUTY_ON (1023) // 100% 占空比

static uint32_t s_vibrate_end_time = 0;
static bool s_vibrate_active = false;

void board_vibrate_init(void) {
  // 配置震动马达GPIO为输出
  gpio_config_t vibrate_config = {
      .pin_bit_mask = (1ULL << BOARD_GPIO_VIBRATE),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&vibrate_config);

  // LEDC定时器配置
  ledc_timer_config_t ledc_timer = {.speed_mode = VIBRATE_LEDC_MODE,
                                    .timer_num = VIBRATE_LEDC_TIMER,
                                    .duty_resolution = VIBRATE_LEDC_RES,
                                    .freq_hz = VIBRATE_LEDC_FREQ,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  // LEDC通道配置
  ledc_channel_config_t ledc_channel = {.speed_mode = VIBRATE_LEDC_MODE,
                                        .channel = VIBRATE_LEDC_CHANNEL,
                                        .timer_sel = VIBRATE_LEDC_TIMER,
                                        .intr_type = LEDC_INTR_DISABLE,
                                        .gpio_num = BOARD_GPIO_VIBRATE,
                                        .duty = 0, // 初始关闭
                                        .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

  // 增强GPIO驱动强度到最大 (LEDC配置后再次确保驱动能力)
  // 注意：如果直接驱动马达，GPIO电流可能不足，建议使用晶体管驱动
  gpio_set_drive_capability(BOARD_GPIO_VIBRATE, GPIO_DRIVE_CAP_3);

  s_vibrate_active = false;
  s_vibrate_end_time = 0;
  ESP_LOGI(BOARD_TAG, "Vibrate motor initialized (PWM %dHz) on GPIO%d",
           VIBRATE_LEDC_FREQ, BOARD_GPIO_VIBRATE);
}

esp_err_t board_vibrate_on(uint32_t ms) {
  // 设置占空比开启震动
  ESP_ERROR_CHECK(
      ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL, VIBRATE_DUTY_ON));
  ESP_ERROR_CHECK(ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL));

  // 先设置end_time再激活标志，确保原子性
  if (ms > 0) {
    s_vibrate_end_time = board_time_ms() + ms;
    s_vibrate_active = true;
  } else {
    // 持续震动：设置end_time为最大值而不是0，避免被tick立即关掉
    s_vibrate_end_time = 0xFFFFFFFF;
    s_vibrate_active = true;
  }
  ESP_LOGI(BOARD_TAG, "Vibrate ON: %u ms", ms);
  return ESP_OK;
}

esp_err_t board_vibrate_off(void) {
  s_vibrate_active = false;
  s_vibrate_end_time = 0;

  // 设置占空比为0关闭震动
  ESP_ERROR_CHECK(ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL, 0));
  ESP_ERROR_CHECK(ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL));

  ESP_LOGI(BOARD_TAG, "Vibrate OFF");
  return ESP_OK;
}

/* ================== 震动状态管理 ================== */
void board_vibrate_tick(void) {
  if (!s_vibrate_active) {
    return; // 未激活，直接返回
  }

  // 0xFFFFFFFF表示持续震动（手动关闭），忽略超时
  if (s_vibrate_end_time == 0xFFFFFFFF) {
    return;
  }

  uint32_t current_time = board_time_ms();
  if (current_time >= s_vibrate_end_time) {
    board_vibrate_off();
  }
}
