#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"    // 必须：用于获取系统时间
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "board_pins.h"   // GPIO引脚定义
#include "board.h"        // 公共接口

// --- 配置 ---
#define VIBRATE_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define VIBRATE_LEDC_CHAN    LEDC_CHANNEL_0
#define VIBRATE_DUTY_MAX     1023
#define PATTERN_MAX_STEPS    8 

static const char *TAG = "VIBRATE";

static struct {
    uint32_t steps[PATTERN_MAX_STEPS];
    uint8_t  current_step;
    uint8_t  total_steps;
    uint32_t next_switch_ms;
    bool     is_initialized;
} s_vibrate_state = {0};

// --- 内部私有工具 ---

static uint32_t _get_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void _set_pwm(uint32_t duty) {
    ledc_set_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHAN, duty);
    ledc_update_duty(VIBRATE_LEDC_MODE, VIBRATE_LEDC_CHAN);
}

// --- 公共接口 ---



void board_vibrate_init(void) {
    if (s_vibrate_state.is_initialized) return;

    ledc_timer_config_t timer_cfg = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .timer_num  = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz    = 200,
        .clk_cfg    = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t chan_cfg = {
        .speed_mode = VIBRATE_LEDC_MODE,
        .channel    = VIBRATE_LEDC_CHAN,
        .timer_sel  = LEDC_TIMER_0,
        .gpio_num   = BOARD_GPIO_VIBRATE,
        .duty       = 0
    };
    ledc_channel_config(&chan_cfg);
    
    gpio_set_drive_capability(BOARD_GPIO_VIBRATE, GPIO_DRIVE_CAP_3);
    s_vibrate_state.is_initialized = true;
    ESP_LOGI(TAG, "Vibrate initialized successfully");
}

void board_vibrate_pattern(const uint32_t* ms_array, uint8_t count) {
    if (!s_vibrate_state.is_initialized || count == 0) {
        ESP_LOGW(TAG, "Vibrate not initialized or invalid pattern");
        return;
    }

    uint8_t steps = (count > PATTERN_MAX_STEPS) ? PATTERN_MAX_STEPS : count;
    for (uint8_t i = 0; i < steps; i++) {
        s_vibrate_state.steps[i] = ms_array[i];
    }
    
    s_vibrate_state.total_steps = steps;
    s_vibrate_state.current_step = 0;
    s_vibrate_state.next_switch_ms = _get_now_ms() + s_vibrate_state.steps[0];
    
    _set_pwm(VIBRATE_DUTY_MAX);
    ESP_LOGD(TAG, "Vibrate pattern started: %d steps", steps);
}

void board_vibrate_off(void) {
    _set_pwm(0);
    s_vibrate_state.total_steps = 0;
    s_vibrate_state.next_switch_ms = 0;
    ESP_LOGD(TAG, "Vibrate stopped");
}

// --- 常用模式快捷封装 ---

void board_vibrate_short(void) {
    const uint32_t p[] = {80};
    board_vibrate_pattern(p, 1);
    ESP_LOGD(TAG, "Vibrate short");
}

void board_vibrate_double(void) {
    const uint32_t p[] = {100, 100, 100};
    board_vibrate_pattern(p, 3);
    ESP_LOGD(TAG, "Vibrate double");
}

// --- 状态轮询 ---

void board_vibrate_tick(void) {
    if (s_vibrate_state.total_steps == 0) return;

    uint32_t now = _get_now_ms();
    if (now >= s_vibrate_state.next_switch_ms) {
        s_vibrate_state.current_step++;
        
        if (s_vibrate_state.current_step >= s_vibrate_state.total_steps) {
            board_vibrate_off(); 
        } else {
            // Step 0, 2, 4... 为震动；Step 1, 3, 5... 为停顿
            bool should_vibrate = (s_vibrate_state.current_step % 2 == 0);
            _set_pwm(should_vibrate ? VIBRATE_DUTY_MAX : 0);
            
            s_vibrate_state.next_switch_ms = now + s_vibrate_state.steps[s_vibrate_state.current_step];
        }
    }
}