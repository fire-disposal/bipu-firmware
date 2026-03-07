#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "board.h"
#include "board_pins.h"

static const char* TAG = "BOARD_VIBRATE";

/* ================== 配置常量 ================== */
#define VIBRATE_LEDC_TIMER      LEDC_TIMER_0
#define VIBRATE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define VIBRATE_LEDC_CHANNEL    LEDC_CHANNEL_0
#define VIBRATE_LEDC_RES        LEDC_TIMER_10_BIT
#define VIBRATE_LEDC_FREQ       200     // 200Hz 震感较强
#define VIBRATE_DUTY_ON         (1023)  // 100% 占空比

/* ================== 模式定义 ================== */
#define MAX_PATTERN_STEPS 8

typedef struct {
    uint32_t durations[MAX_PATTERN_STEPS]; // 震动模式时间数组 (ms)
    uint8_t count;                         // 步骤总数
    uint8_t current_idx;                   // 当前步骤索引
    uint32_t next_switch_time;             // 下次切换的时间点
    bool active;                           // 是否处于活动状态
} vib_state_t;

static vib_state_t s_vib = {0};
static bool s_vibrate_initialized = false;

/* 预定义模式 */
static const uint32_t PATTERN_SHORT[] = { 150 };
static const uint32_t PATTERN_DOUBLE[] = { 150, 100, 150 };

/* ================== 内部辅助函数 ================== */

static void vibrate_set_pwm(uint32_t duty) {
    ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL, duty);
    ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL);
}

static void stop_vibration(void) {
    s_vib.active = false;
    vibrate_set_pwm(0);
}

/* ================== 公开 API 实现 ================== */

void board_vibrate_init(void) {
    if (s_vibrate_initialized) {
        ESP_LOGW(TAG, "Vibrate motor already initialized");
        return;
    }

    // GPIO 配置
    gpio_config_t vibrate_config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_VIBRATE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&vibrate_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Vibrate GPIO config failed: %s", esp_err_to_name(ret));
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
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
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
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    // 增强 GPIO 驱动强度
    gpio_set_drive_capability(BOARD_GPIO_VIBRATE, GPIO_DRIVE_CAP_3);

    s_vibrate_initialized = true;
    stop_vibration();
    
    ESP_LOGI(TAG, "Vibrate motor initialized (PWM %dHz) on GPIO%d", 
             VIBRATE_LEDC_FREQ, BOARD_GPIO_VIBRATE);
}

void board_vibrate_pattern(const uint32_t* ms_array, uint8_t count) {
    if (!s_vibrate_initialized) return;
    
    // 允许覆盖：重置状态
    stop_vibration();

    if (!ms_array || count == 0) return;

    if (count > MAX_PATTERN_STEPS) {
        count = MAX_PATTERN_STEPS;
        ESP_LOGW(TAG, "Pattern truncated to %d steps", MAX_PATTERN_STEPS);
    }

    // 复制模式数据到静态缓冲区，避免外部内存释放问题
    for (int i=0; i<count; i++) {
        s_vib.durations[i] = ms_array[i];
    }
    
    s_vib.count = count;
    s_vib.current_idx = 0;
    s_vib.active = true;
    
    // 立即启动第一步 (ON)
    vibrate_set_pwm(VIBRATE_DUTY_ON);
    s_vib.next_switch_time = board_time_ms() + s_vib.durations[0];
    
    ESP_LOGD(TAG, "Vibrate pattern started (%d steps)", count);
}

void board_vibrate_short(void) {
    board_vibrate_pattern(PATTERN_SHORT, sizeof(PATTERN_SHORT)/sizeof(uint32_t));
}

void board_vibrate_double(void) {
    board_vibrate_pattern(PATTERN_DOUBLE, sizeof(PATTERN_DOUBLE)/sizeof(uint32_t));
}

void board_vibrate_off(void) {
    if (!s_vibrate_initialized) return;
    stop_vibration();
    ESP_LOGD(TAG, "Vibrate forced OFF");
}

void board_vibrate_tick(void) {
    if (!s_vibrate_initialized || !s_vib.active) return;

    uint32_t now = board_time_ms();
    
    if (now >= s_vib.next_switch_time) {
        s_vib.current_idx++;
        
        if (s_vib.current_idx >= s_vib.count) {
            // 模式结束
            stop_vibration();
            ESP_LOGD(TAG, "Vibrate pattern completed");
        } else {
            // 切换状态: 偶数索引=ON, 奇数索引=OFF
            // 索引 0 (ON) -> 1 (OFF) -> 2 (ON) ...
            bool turn_on = (s_vib.current_idx % 2 == 0);
            vibrate_set_pwm(turn_on ? VIBRATE_DUTY_ON : 0);
            
            s_vib.next_switch_time = now + s_vib.durations[s_vib.current_idx];
        }
    }
}

bool board_vibrate_is_active(void) {
    return s_vibrate_initialized && s_vib.active;
}

