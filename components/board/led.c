#include "board_pins.h"   // GPIO引脚定义
#include "board.h"        // 公共接口
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

/* ================== LED 状态机定义 ================== */

typedef enum {
    LED_SM_IDLE,            // 空闲状态
    LED_SM_FLASHLIGHT,      // 手电筒常亮
    LED_SM_SHORT_FLASH,     // 短闪一次
    LED_SM_DOUBLE_FLASH,    // 快速闪动 2 次
    LED_SM_CONTINUOUS_BLINK,// 持续闪灭
    LED_SM_GALLOP,          // 三灯跑马
} led_sm_state_t;

/* ================== 配置常量 ================== */
#define LED_SHORT_FLASH_ON_MS     150   // 短闪亮的时间
#define LED_SHORT_FLASH_OFF_MS    100   // 短闪灭的时间
#define LED_DOUBLE_FLASH_ON_MS    100   // 双闪每次亮的时间
#define LED_DOUBLE_FLASH_GAP_MS   100   // 双闪间隔
#define LED_DOUBLE_FLASH_CYCLE_MS 400   // 双闪一个周期 (亮 - 灭 - 亮 - 灭)
#define LED_CONTINUOUS_INTERVAL_MS 200  // 持续闪灭间隔
#define LED_GALLOP_INTERVAL_MS    150   // 跑马灯间隔

/* ================== 模块状态 ================== */
static bool s_leds_initialized = false;
static board_leds_t s_current_leds = {0, 0, 0};
static SemaphoreHandle_t s_leds_mutex = NULL;

// 状态机状态
static led_sm_state_t s_sm_state = LED_SM_IDLE;
static uint32_t s_sm_start_time = 0;
static uint32_t s_sm_last_change = 0;
static int s_sm_sub_state = 0;      // 子状态（用于多阶段效果）
static int s_sm_blink_count = 0;    // 闪烁计数
static int s_sm_gallop_index = 0;   // 跑马灯索引

/* ================== 内部辅助函数 ================== */

// 安全获取互斥锁
static inline bool leds_lock(void) {
    if (s_leds_mutex == NULL) return true;
    return xSemaphoreTake(s_leds_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static inline void leds_unlock(void) {
    if (s_leds_mutex != NULL) {
        xSemaphoreGive(s_leds_mutex);
    }
}

// 直接设置 GPIO（不经过状态机）
static void leds_set_raw(board_leds_t leds) {
    gpio_set_level(BOARD_GPIO_LED_1, leds.led1 > 127 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_2, leds.led2 > 127 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_3, leds.led3 > 127 ? 1 : 0);
    s_current_leds = leds;
}

// 设置状态机为空闲
static void sm_set_idle(void) {
    s_sm_state = LED_SM_IDLE;
    s_sm_start_time = 0;
    s_sm_last_change = 0;
    s_sm_sub_state = 0;
    s_sm_blink_count = 0;
    s_sm_gallop_index = 0;
}

/* ================== 公开 API 实现 ================== */

void board_leds_init(void) {
    if (s_leds_initialized) {
        ESP_LOGW(BOARD_TAG, "LEDs already initialized");
        return;
    }

    // 创建互斥锁
    s_leds_mutex = xSemaphoreCreateMutex();
    if (s_leds_mutex == NULL) {
        ESP_LOGE(BOARD_TAG, "Failed to create LED mutex");
    }

    // 接管引脚（避免上电默认电平）
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
    
    esp_err_t ret = gpio_config(&led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "LED GPIO config failed: %s", esp_err_to_name(ret));
        return;
    }

    // 立即熄灭
    leds_set_raw((board_leds_t){0, 0, 0});
    
    s_leds_initialized = true;
    sm_set_idle();
    ESP_LOGI(BOARD_TAG, "LEDs initialized (GPIO)");
}

void board_leds_set(board_leds_t leds) {
    if (!s_leds_initialized) {
        ESP_LOGW(BOARD_TAG, "LEDs not initialized");
        return;
    }

    if (!leds_lock()) {
        ESP_LOGW(BOARD_TAG, "Failed to acquire LED lock");
        return;
    }

    // 如果当前是手电筒模式，直接设置并返回（手电筒优先级最高）
    if (s_sm_state == LED_SM_FLASHLIGHT) {
        leds_set_raw(leds);
    } else {
        // 其他模式下，设置当前状态但可能被状态机覆盖
        leds_set_raw(leds);
    }
    
    leds_unlock();
}

void board_leds_off(void) {
    if (!s_leds_initialized) {
        return;
    }

    if (!leds_lock()) {
        ESP_LOGW(BOARD_TAG, "Failed to acquire LED lock");
        return;
    }

    // 手电筒模式下不关闭
    if (s_sm_state != LED_SM_FLASHLIGHT) {
        leds_set_raw((board_leds_t){0, 0, 0});
    }
    
    leds_unlock();
}

board_leds_t board_leds_get_state(void) {
    return s_current_leds;
}

bool board_leds_is_initialized(void) {
    return s_leds_initialized;
}

/* ================== LED 状态机控制 API ================== */

void board_leds_flashlight_on(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    s_sm_state = LED_SM_FLASHLIGHT;
    s_sm_start_time = board_time_ms();
    // 手电筒：三灯全亮
    leds_set_raw((board_leds_t){255, 255, 255});
    
    leds_unlock();
    ESP_LOGD(BOARD_TAG, "Flashlight ON");
}

void board_leds_flashlight_off(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state == LED_SM_FLASHLIGHT) {
        sm_set_idle();
        leds_set_raw((board_leds_t){0, 0, 0});
        ESP_LOGD(BOARD_TAG, "Flashlight OFF");
    }
    
    leds_unlock();
}

bool board_leds_is_flashlight_on(void) {
    return s_sm_state == LED_SM_FLASHLIGHT;
}

void board_leds_short_flash(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    // 中断当前非手电筒状态
    if (s_sm_state != LED_SM_FLASHLIGHT) {
        s_sm_state = LED_SM_SHORT_FLASH;
        s_sm_start_time = board_time_ms();
        s_sm_last_change = board_time_ms();
        s_sm_sub_state = 0;  // 0=亮
        leds_set_raw((board_leds_t){255, 255, 255});
        ESP_LOGD(BOARD_TAG, "Short flash started");
    }
    
    leds_unlock();
}

void board_leds_double_flash(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state != LED_SM_FLASHLIGHT) {
        s_sm_state = LED_SM_DOUBLE_FLASH;
        s_sm_start_time = board_time_ms();
        s_sm_last_change = board_time_ms();
        s_sm_sub_state = 0;  // 0=第一次亮
        s_sm_blink_count = 0;
        leds_set_raw((board_leds_t){255, 255, 255});
        ESP_LOGD(BOARD_TAG, "Double flash started");
    }
    
    leds_unlock();
}

void board_leds_continuous_blink_start(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state != LED_SM_FLASHLIGHT) {
        s_sm_state = LED_SM_CONTINUOUS_BLINK;
        s_sm_start_time = board_time_ms();
        s_sm_last_change = board_time_ms();
        s_sm_sub_state = 0;  // 0=亮
        leds_set_raw((board_leds_t){255, 255, 255});
        ESP_LOGD(BOARD_TAG, "Continuous blink started");
    }
    
    leds_unlock();
}

void board_leds_continuous_blink_stop(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state == LED_SM_CONTINUOUS_BLINK) {
        sm_set_idle();
        leds_set_raw((board_leds_t){0, 0, 0});
        ESP_LOGD(BOARD_TAG, "Continuous blink stopped");
    }
    
    leds_unlock();
}

void board_leds_gallop_start(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state != LED_SM_FLASHLIGHT) {
        s_sm_state = LED_SM_GALLOP;
        s_sm_start_time = board_time_ms();
        s_sm_last_change = board_time_ms();
        s_sm_gallop_index = 0;
        // 点亮第一个灯
        leds_set_raw((board_leds_t){255, 0, 0});
        ESP_LOGD(BOARD_TAG, "Gallop started");
    }
    
    leds_unlock();
}

void board_leds_gallop_stop(void) {
    if (!s_leds_initialized) return;
    
    if (!leds_lock()) return;
    
    if (s_sm_state == LED_SM_GALLOP) {
        sm_set_idle();
        leds_set_raw((board_leds_t){0, 0, 0});
        ESP_LOGD(BOARD_TAG, "Gallop stopped");
    }
    
    leds_unlock();
}

/* ================== LED 状态机轮询 ================== */
void board_leds_tick(void) {
    if (!s_leds_initialized || s_sm_state == LED_SM_IDLE) {
        return;
    }

    uint32_t now = board_time_ms();

    switch (s_sm_state) {
        case LED_SM_FLASHLIGHT:
            // 手电筒保持常亮，无需处理
            break;

        case LED_SM_SHORT_FLASH: {
            // 短闪一次：亮 150ms -> 灭 100ms -> 结束
            if (s_sm_sub_state == 0) {
                // 亮状态
                if (now - s_sm_last_change >= LED_SHORT_FLASH_ON_MS) {
                    s_sm_sub_state = 1;
                    s_sm_last_change = now;
                    leds_set_raw((board_leds_t){0, 0, 0});
                }
            } else {
                // 灭状态
                if (now - s_sm_last_change >= LED_SHORT_FLASH_OFF_MS) {
                    sm_set_idle();
                    ESP_LOGD(BOARD_TAG, "Short flash completed");
                }
            }
            break;
        }

        case LED_SM_DOUBLE_FLASH: {
            // 快速闪动 2 次：亮 100ms -> 灭 100ms -> 亮 100ms -> 灭 100ms -> 结束
            if (s_sm_blink_count < 4) {  // 4 个半周期
                if (now - s_sm_last_change >= LED_DOUBLE_FLASH_ON_MS) {
                    s_sm_last_change = now;
                    if (s_sm_blink_count % 2 == 0) {
                        // 灭
                        leds_set_raw((board_leds_t){0, 0, 0});
                    } else {
                        // 亮
                        leds_set_raw((board_leds_t){255, 255, 255});
                    }
                    s_sm_blink_count++;
                }
            } else {
                sm_set_idle();
                ESP_LOGD(BOARD_TAG, "Double flash completed");
            }
            break;
        }

        case LED_SM_CONTINUOUS_BLINK: {
            // 持续闪灭：每 200ms 切换状态
            if (now - s_sm_last_change >= LED_CONTINUOUS_INTERVAL_MS) {
                s_sm_last_change = now;
                s_sm_sub_state = !s_sm_sub_state;
                if (s_sm_sub_state) {
                    leds_set_raw((board_leds_t){255, 255, 255});
                } else {
                    leds_set_raw((board_leds_t){0, 0, 0});
                }
            }
            break;
        }

        case LED_SM_GALLOP: {
            // 三灯跑马：LED1 -> LED2 -> LED3 -> 循环
            if (now - s_sm_last_change >= LED_GALLOP_INTERVAL_MS) {
                s_sm_last_change = now;
                s_sm_gallop_index = (s_sm_gallop_index + 1) % 3;
                
                board_leds_t leds = {0, 0, 0};
                if (s_sm_gallop_index == 0) leds.led1 = 255;
                else if (s_sm_gallop_index == 1) leds.led2 = 255;
                else if (s_sm_gallop_index == 2) leds.led3 = 255;
                leds_set_raw(leds);
            }
            break;
        }

        default:
            sm_set_idle();
            break;
    }
}

/* ================== 状态查询 ================== */
bool board_leds_is_active(void) {
    return s_sm_state != LED_SM_IDLE;
}
