#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "board.h"
#include "board_pins.h"

static const char* TAG = "BOARD_VIBRATE";

/* ================== 配置常量 ================== */
#define VIBRATE_LEDC_TIMER      LEDC_TIMER_0
#define VIBRATE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define VIBRATE_LEDC_CHANNEL    LEDC_CHANNEL_0
#define VIBRATE_LEDC_RES        LEDC_TIMER_10_BIT
#define VIBRATE_LEDC_FREQ       200     // 200Hz PWM 频率
#define VIBRATE_DUTY_MAX        1023    // 100% 占空比 (10-bit resolution)
#define MAX_PATTERN_STEPS       16

/* ================== 震动状态 ================== */
typedef struct {
    uint32_t durations[MAX_PATTERN_STEPS];  // 时间数组 (ms)
    uint8_t count;                          // 步骤数
    uint8_t step_idx;                       // 当前步骤
    uint32_t step_start_time;               // 步骤开始时间
    bool is_on;                             // 当前是开还是关
    bool active;                            // 是否活动
} vibrate_state_t;

static vibrate_state_t s_vib = {0};
static bool s_initialized = false;

/* ================== 内部辅助函数 ================== */

// 设置 PWM 占空比
static void set_vibrate_pwm(uint32_t duty) {
    if (!s_initialized) return;
    ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL, duty);
    ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHANNEL);
}

// 停止震动
static void stop_vibrate(void) {
    s_vib.active = false;
    set_vibrate_pwm(0);
    ESP_LOGD(TAG, "Vibration stopped");
}

/* ================== 公开 API ================== */

void board_vibrate_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    // 重置GPIO确保干净的状态
    gpio_reset_pin(BOARD_GPIO_VIBRATE);

    // LEDC 定时器配置
    ledc_timer_config_t timer_cfg = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .timer_num = VIBRATE_LEDC_TIMER,
        .duty_resolution = VIBRATE_LEDC_RES,
        .freq_hz = VIBRATE_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Timer config failed");
        return;
    }
    
    // LEDC 通道配置
    ledc_channel_config_t channel_cfg = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .channel = VIBRATE_LEDC_CHANNEL,
        .timer_sel = VIBRATE_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_GPIO_VIBRATE,
        .duty = 0,
        .hpoint = 0
    };
    
    if (ledc_channel_config(&channel_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Channel config failed");
        return;
    }
    
    s_initialized = true;
    stop_vibrate();
    
    ESP_LOGI(TAG, "Vibrate initialized on GPIO%d, %dHz PWM", 
             BOARD_GPIO_VIBRATE, VIBRATE_LEDC_FREQ);
}

// 启动自定义模式
void board_vibrate_pattern(const uint32_t* ms_array, uint8_t count) {
    if (!s_initialized || !ms_array || count == 0) {
        return;
    }
    
    // 停止当前模式
    stop_vibrate();
    
    // 限制步骤数
    if (count > MAX_PATTERN_STEPS) {
        count = MAX_PATTERN_STEPS;
    }
    
    // 复制模式到状态机
    for (uint8_t i = 0; i < count; i++) {
        s_vib.durations[i] = ms_array[i];
    }
    
    s_vib.count = count;
    s_vib.step_idx = 0;
    s_vib.step_start_time = board_time_ms();
    s_vib.is_on = true;  // 第一步总是 ON
    s_vib.active = true;
    
    // 立即启动
    set_vibrate_pwm(VIBRATE_DUTY_MAX);
    
    ESP_LOGD(TAG, "Pattern started: %d steps", count);
}

void board_vibrate_short(void) {
    uint32_t pattern[] = { 150 };
    board_vibrate_pattern(pattern, 1);
}

void board_vibrate_double(void) {
    uint32_t pattern[] = { 100, 80, 100 };
    board_vibrate_pattern(pattern, 3);
}

void board_vibrate_off(void) {
    stop_vibrate();
}

// 定时器：应在 app_loop 中每 1-10ms 调用一次
void board_vibrate_tick(void) {
    if (!s_initialized || !s_vib.active) {
        return;
    }
    
    uint32_t now = board_time_ms();
    uint32_t elapsed = now - s_vib.step_start_time;
    
    // 检查当前步骤是否时间到期
    if (elapsed >= s_vib.durations[s_vib.step_idx]) {
        // 移动到下一步
        s_vib.step_idx++;
        
        if (s_vib.step_idx >= s_vib.count) {
            // 模式完成
            stop_vibrate();
            return;
        }
        
        // 切换状态：ON -> OFF -> ON ...
        s_vib.is_on = !s_vib.is_on;
        s_vib.step_start_time = now;
        
        // 设置新的 PWM 占空比
        set_vibrate_pwm(s_vib.is_on ? VIBRATE_DUTY_MAX : 0);
        
        ESP_LOGD(TAG, "Step %d: %s", s_vib.step_idx, s_vib.is_on ? "ON" : "OFF");
    }
}

bool board_vibrate_is_active(void) {
    return s_initialized && s_vib.active;
}

