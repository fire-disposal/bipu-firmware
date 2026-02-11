#include "board.h"
#include "board_hal.h"
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
    ESP_LOGI(BOARD_TAG, "LEDs initialized (GPIO)");
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
