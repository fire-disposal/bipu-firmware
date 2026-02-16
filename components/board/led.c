#include "board_pins.h"   // GPIO引脚定义
#include "board.h"        // 公共接口
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ================== 三个独立白光 LED 接口实现 ================== */

// 模块状态
static bool s_leds_initialized = false;
static board_leds_t s_current_leds = {0, 0, 0};
static SemaphoreHandle_t s_leds_mutex = NULL;

// 安全获取互斥锁
static inline bool leds_lock(void) {
    if (s_leds_mutex == NULL) return true; // 未初始化时不阻塞
    return xSemaphoreTake(s_leds_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static inline void leds_unlock(void) {
    if (s_leds_mutex != NULL) {
        xSemaphoreGive(s_leds_mutex);
    }
}

void board_leds_init(void) {
    if (s_leds_initialized) {
        ESP_LOGW(BOARD_TAG, "LEDs already initialized");
        return;
    }

    // 创建互斥锁
    s_leds_mutex = xSemaphoreCreateMutex();
    if (s_leds_mutex == NULL) {
        ESP_LOGE(BOARD_TAG, "Failed to create LED mutex");
        // 继续初始化，但无线程安全保护
    }

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
    
    esp_err_t ret = gpio_config(&led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "LED GPIO config failed: %s", esp_err_to_name(ret));
        return;
    }

    // 立即熄灭以覆盖上电的默认电平（尤其是 GPIO2）
    gpio_set_level(BOARD_GPIO_LED_1, 0);
    gpio_set_level(BOARD_GPIO_LED_2, 0);
    gpio_set_level(BOARD_GPIO_LED_3, 0);
    
    s_current_leds = (board_leds_t){0, 0, 0};
    s_leds_initialized = true;
    ESP_LOGI(BOARD_TAG, "LEDs initialized successfully");
}

void board_leds_set(board_leds_t leds) {
    if (!s_leds_initialized) {
        ESP_LOGW(BOARD_TAG, "LEDs not initialized, call board_leds_init() first");
        return;
    }

    if (!leds_lock()) {
        ESP_LOGW(BOARD_TAG, "Failed to acquire LED lock");
        return;
    }

    // 使用阈值控制三路白光灯（>127 视为点亮）
    gpio_set_level(BOARD_GPIO_LED_1, leds.led1 > 127 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_2, leds.led2 > 127 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_3, leds.led3 > 127 ? 1 : 0);
    
    s_current_leds = leds;
    leds_unlock();
}

void board_leds_off(void) {
    if (!s_leds_initialized) {
        return; // 静默返回，避免在初始化前调用时产生警告
    }

    if (!leds_lock()) {
        ESP_LOGW(BOARD_TAG, "Failed to acquire LED lock for off");
        return;
    }

    gpio_set_level(BOARD_GPIO_LED_1, 0);
    gpio_set_level(BOARD_GPIO_LED_2, 0);
    gpio_set_level(BOARD_GPIO_LED_3, 0);
    
    s_current_leds = (board_leds_t){0, 0, 0};
    leds_unlock();
}

/* 获取当前LED状态（用于调试或状态查询） */
board_leds_t board_leds_get_state(void) {
    return s_current_leds;
}

bool board_leds_is_initialized(void) {
    return s_leds_initialized;
}

/* ================== LED 状态机（通用设计，不涉及应用层逻辑） ================== */

typedef struct {
    board_led_mode_t mode;
    uint32_t mode_enter_time;
    uint32_t last_change_time;
    int marquee_idx;                   // 跑马灯索引
    bool notify_pending;               // 通知闪烁待处理
} led_sm_t;

static led_sm_t s_led_sm = {
    .mode = BOARD_LED_MODE_OFF,
    .mode_enter_time = 0,
    .last_change_time = 0,
    .marquee_idx = 0,
    .notify_pending = false,
};

// LED 模式参数
#define LED_MARQUEE_INTERVAL_MS        300
#define LED_BLINK_INTERVAL_MS          200
#define LED_BLINK_DURATION_MS          3000
#define LED_NOTIFY_FLASH_DURATION_MS   1000
#define LED_NOTIFY_PHASE_MS            250

/**
 * 设置 LED 工作模式
 */
void board_leds_set_mode(board_led_mode_t mode)
{
    if (s_led_sm.mode == mode) {
        return;
    }
    
    uint32_t now = board_time_ms();
    s_led_sm.mode = mode;
    s_led_sm.mode_enter_time = now;
    s_led_sm.last_change_time = now;
    s_led_sm.marquee_idx = 0;
}

/**
 * LED 通知闪烁（优先级最高）
 */
void board_leds_notify(void)
{
    s_led_sm.notify_pending = true;
}

/**
 * LED 状态机更新（需要在主循环中定期调用）
 */
void board_leds_tick(void)
{
    if (!s_leds_initialized) {
        return;
    }
    
    uint32_t now = board_time_ms();
    
    // 通知闪烁：最高优先级
    if (s_led_sm.notify_pending) {
        uint32_t notify_elapsed = now - s_led_sm.mode_enter_time;
        
        if (notify_elapsed >= LED_NOTIFY_FLASH_DURATION_MS) {
            // 通知闪烁完成，恢复模式
            s_led_sm.notify_pending = false;
            s_led_sm.mode_enter_time = now;
            s_led_sm.last_change_time = now;
        } else {
            // 快速闪烁：两次，每次 250ms
            bool phase = ((notify_elapsed / LED_NOTIFY_PHASE_MS) % 2) == 0;
            board_leds_t leds = {0, 0, 0};
            if (phase) {
                leds.led1 = leds.led2 = leds.led3 = 255;
            }
            board_leds_set(leds);
            return;
        }
    }
    
    // 正常模式处理
    switch (s_led_sm.mode) {
        case BOARD_LED_MODE_OFF: {
            board_leds_off();
            break;
        }
        
        case BOARD_LED_MODE_STATIC: {
            // 静态点亮：全亮（用于手电筒）
            board_leds_t leds = {255, 255, 255};
            board_leds_set(leds);
            break;
        }
        
        case BOARD_LED_MODE_MARQUEE: {
            // 跑马灯：轮流点亮三个 LED，每 300ms 切换
            if (now - s_led_sm.last_change_time >= LED_MARQUEE_INTERVAL_MS) {
                s_led_sm.last_change_time = now;
                board_leds_t leds = {0, 0, 0};
                if (s_led_sm.marquee_idx == 0) leds.led1 = 255;
                else if (s_led_sm.marquee_idx == 1) leds.led2 = 255;
                else leds.led3 = 255;
                board_leds_set(leds);
                s_led_sm.marquee_idx = (s_led_sm.marquee_idx + 1) % 3;
            }
            break;
        }
        
        case BOARD_LED_MODE_BLINK: {
            // 闪烁：3 秒内每 200ms 切换一次亮灭
            uint32_t elapsed = now - s_led_sm.mode_enter_time;
            
            if (elapsed >= LED_BLINK_DURATION_MS) {
                // 闪烁完成，转到关闭模式
                board_leds_set_mode(BOARD_LED_MODE_OFF);
            } else {
                if (now - s_led_sm.last_change_time >= LED_BLINK_INTERVAL_MS) {
                    s_led_sm.last_change_time = now;
                    bool on = ((elapsed / LED_BLINK_INTERVAL_MS) % 2) == 0;
                    board_leds_t leds = {0, 0, 0};
                    if (on) {
                        leds.led1 = leds.led2 = leds.led3 = 255;
                    }
                    board_leds_set(leds);
                }
            }
            break;
        }
        
        case BOARD_LED_MODE_NOTIFY_FLASH: {
            // 不应到达此处
            break;
        }
    }
}