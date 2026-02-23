#include "board.h"
#include "board_hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>

/* ================== 震动状态机定义 ================== */

typedef enum {
    VIB_SM_IDLE,           // 空闲状态
    VIB_SM_SHORT,          // 短震动
    VIB_SM_DOUBLE,         // 震动两次
} vib_sm_state_t;

/* ================== 配置常量 ================== */
// PWM 配置参数
#define VIBRATE_LEDC_TIMER      LEDC_TIMER_0
#define VIBRATE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define VIBRATE_LEDC_CHANNEL    LEDC_CHANNEL_0
#define VIBRATE_LEDC_RES        LEDC_TIMER_10_BIT
#define VIBRATE_LEDC_FREQ       200     // 降低频率以获得更强的震感
#define VIBRATE_DUTY_ON         (1023)  // 100% 占空比

// 震动时序配置
#define VIB_SHORT_ON_MS         150     // 短震动持续时间
#define VIB_DOUBLE_ON_MS        150     // 双震每次持续时间
#define VIB_DOUBLE_GAP_MS       100     // 双震间隔

/* ================== 模块状态 ================== */
static vib_sm_state_t s_sm_state = VIB_SM_IDLE;
static uint32_t s_sm_start_time = 0;
static uint32_t s_sm_last_change = 0;
static int s_sm_sub_state = 0;          // 子状态 (0=第一次亮，1=间隔，2=第二次亮)
static bool s_vibrate_initialized = false;

/* ================== 内部辅助函数 ================== */

// 设置 PWM 占空比
static void vibrate_set_pwm(uint32_t duty) {
    ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL, duty);
    ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL);
}

// 设置状态机为空闲
static void sm_set_idle(void) {
    s_sm_state = VIB_SM_IDLE;
    s_sm_start_time = 0;
    s_sm_last_change = 0;
    s_sm_sub_state = 0;
    vibrate_set_pwm(0);
}

/* ================== 公开 API 实现 ================== */

void board_vibrate_init(void) {
    if (s_vibrate_initialized) {
        ESP_LOGW(BOARD_TAG, "Vibrate motor already initialized");
        return;
    }

    // 配置震动马达 GPIO 为输出
    gpio_config_t vibrate_config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_VIBRATE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&vibrate_config);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Vibrate GPIO config failed: %s", esp_err_to_name(ret));
        return;
    }

    // LEDC 定时器配置
    ledc_timer_config_t ledc_timer = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .timer_num = VIBRATE_LEDC_TIMER,
        .duty_resolution = VIBRATE_LEDC_RES,
        .freq_hz = VIBRATE_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    // LEDC 通道配置
    ledc_channel_config_t ledc_channel = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .channel = VIBRATE_LEDC_CHANNEL,
        .timer_sel = VIBRATE_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_GPIO_VIBRATE,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    // 增强 GPIO 驱动强度
    gpio_set_drive_capability(BOARD_GPIO_VIBRATE, GPIO_DRIVE_CAP_3);

    s_vibrate_initialized = true;
    sm_set_idle();
    
    ESP_LOGI(BOARD_TAG, "Vibrate motor initialized (PWM %dHz) on GPIO%d",
             VIBRATE_LEDC_FREQ, BOARD_GPIO_VIBRATE);
}

void board_vibrate_short(void) {
    if (!s_vibrate_initialized) return;
    
    // 如果已经在震动中，不中断（简单优先级）
    if (s_sm_state != VIB_SM_IDLE) return;
    
    s_sm_state = VIB_SM_SHORT;
    s_sm_start_time = board_time_ms();
    s_sm_last_change = board_time_ms();
    s_sm_sub_state = 0;  // 0=震动
    vibrate_set_pwm(VIBRATE_DUTY_ON);
    
    ESP_LOGD(BOARD_TAG, "Short vibrate started");
}

void board_vibrate_double(void) {
    if (!s_vibrate_initialized) return;
    
    // 如果已经在震动中，不中断
    if (s_sm_state != VIB_SM_IDLE) return;
    
    s_sm_state = VIB_SM_DOUBLE;
    s_sm_start_time = board_time_ms();
    s_sm_last_change = board_time_ms();
    s_sm_sub_state = 0;  // 0=第一次震动
    vibrate_set_pwm(VIBRATE_DUTY_ON);
    
    ESP_LOGD(BOARD_TAG, "Double vibrate started");
}

void board_vibrate_off(void) {
    if (!s_vibrate_initialized) return;
    
    sm_set_idle();
    ESP_LOGD(BOARD_TAG, "Vibrate forced OFF");
}

/* ================== 震动状态机轮询 ================== */
void board_vibrate_tick(void) {
    if (!s_vibrate_initialized || s_sm_state == VIB_SM_IDLE) {
        return;
    }

    uint32_t now = board_time_ms();

    switch (s_sm_state) {
        case VIB_SM_SHORT: {
            // 短震动：震动 150ms -> 停止
            if (now - s_sm_last_change >= VIB_SHORT_ON_MS) {
                sm_set_idle();
                ESP_LOGD(BOARD_TAG, "Short vibrate completed");
            }
            break;
        }

        case VIB_SM_DOUBLE: {
            // 震动两次：震 150ms -> 停 100ms -> 震 150ms -> 停
            if (s_sm_sub_state == 0) {
                // 第一次震动
                if (now - s_sm_last_change >= VIB_DOUBLE_ON_MS) {
                    s_sm_sub_state = 1;
                    s_sm_last_change = now;
                    vibrate_set_pwm(0);  // 停止（间隔）
                }
            } else if (s_sm_sub_state == 1) {
                // 间隔
                if (now - s_sm_last_change >= VIB_DOUBLE_GAP_MS) {
                    s_sm_sub_state = 2;
                    s_sm_last_change = now;
                    vibrate_set_pwm(VIBRATE_DUTY_ON);  // 第二次震动
                }
            } else {
                // 第二次震动
                if (now - s_sm_last_change >= VIB_DOUBLE_ON_MS) {
                    sm_set_idle();
                    ESP_LOGD(BOARD_TAG, "Double vibrate completed");
                }
            }
            break;
        }

        default:
            sm_set_idle();
            break;
    }
}

/* ================== 状态查询 ================== */
bool board_vibrate_is_active(void) {
    return s_vibrate_initialized && s_sm_state != VIB_SM_IDLE;
}

bool board_vibrate_is_initialized(void) {
    return s_vibrate_initialized;
}
