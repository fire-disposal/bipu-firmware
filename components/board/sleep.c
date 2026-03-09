#include "sleep.h"
#include "board.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 兼容不同 IDF 版本的宏 */
#ifndef PD_MS_TO_S
#define PD_MS_TO_S(ms) ((uint64_t)(ms) / 1000U)
#endif

static const char* TAG = "power_mgmt";

/* ================== 模块状态 ================== */

static power_state_t s_current_state = POWER_STATE_ACTIVE;
static uint32_t s_last_activity_time = 0;
static bool s_initialized = false;

/* ================== 唤醒原因标记（RTC 内存保持） ================== */

/** 
 * RTC 数据段，在 Light Sleep 期间保持
 * 用于唤醒后判断唤醒原因
 */
RTC_DATA_ATTR static bool s_wakeup_from_sleep = false;
RTC_DATA_ATTR static esp_sleep_source_t s_wakeup_cause = ESP_SLEEP_WAKEUP_UNDEFINED;

/* ================== 辅助函数 ================== */

/**
 * @brief 更新最后活动时间
 * 
 * 内部调用，避免外部模块直接修改
 */
static void update_activity_time(void)
{
    s_last_activity_time = board_time_ms();
}

/* ================== 公开 API 实现 ================== */

esp_err_t board_power_mgmt_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Power management already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing power management...");

    // 检查唤醒原因
    esp_sleep_source_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        // 从睡眠唤醒
        s_wakeup_from_sleep = true;
        s_wakeup_cause = cause;
        
        ESP_LOGI(TAG, "Wakeup from sleep: cause=%d", cause);
        
        // 根据唤醒源恢复硬件
        switch (cause) {
            case ESP_SLEEP_WAKEUP_GPIO:
                ESP_LOGI(TAG, "Wakeup from GPIO (button press)");
                break;
                
            case ESP_SLEEP_WAKEUP_UART:
                ESP_LOGI(TAG, "Wakeup from UART (BLE event)");
                break;
                
            default:
                ESP_LOGI(TAG, "Wakeup from other source: %d", cause);
                break;
        }
        
        // 恢复硬件状态（显示、BLE 等）
        board_power_mgmt_wake_up();
    } else {
        // 冷启动
        ESP_LOGI(TAG, "Cold boot (no sleep wakeup)");
        s_wakeup_from_sleep = false;
    }

    // 初始化活动计时器
    update_activity_time();
    
    s_initialized = true;
    s_current_state = POWER_STATE_ACTIVE;
    
    ESP_LOGI(TAG, "Power management initialized (initial state: ACTIVE)");
    return ESP_OK;
}

power_state_t board_power_mgmt_get_state(void)
{
    return s_current_state;
}

uint32_t board_power_mgmt_get_last_activity_time(void)
{
    return s_last_activity_time;
}

void board_power_mgmt_reset_activity_timer(void)
{
    update_activity_time();
    ESP_LOGD(TAG, "Activity timer reset");
}

esp_err_t board_power_mgmt_enter_state(power_state_t state)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (state == s_current_state) {
        return ESP_OK;  // 已在目标状态
    }

    ESP_LOGI(TAG, "Entering power state: %d (from %d)", state, s_current_state);

    switch (state) {
        case POWER_STATE_ACTIVE:
            // 从黑屏/睡眠恢复
            board_display_set_contrast(100);  // 恢复默认亮度
            // BLE 已在唤醒时自动恢复
            break;

        case POWER_STATE_BLACK_SCREEN:
            // 关闭显示，保持 CPU 和 BLE 运行
            board_display_set_contrast(0);  // 关闭 OLED
            board_leds_off();               // 关闭 LED
            break;

        case POWER_STATE_LIGHT_SLEEP:
            // 进入 Light Sleep（不会返回）
            board_power_mgmt_enter_light_sleep();
            // ← 唤醒后从此处继续执行
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    s_current_state = state;
    return ESP_OK;
}

void board_power_mgmt_wake_up(void)
{
    ESP_LOGI(TAG, "Waking up from sleep");
    
    // 恢复显示（但先不亮屏，由 UI 层控制）
    // board_display_set_contrast() 会在 UI 唤醒时调用
    
    // 恢复 BLE（如果需要）
    // NimBLE 会在唤醒后自动恢复
    
    // 短震动提示唤醒
    board_vibrate_short();
    
    ESP_LOGI(TAG, "Wake-up completed");
}

bool board_power_mgmt_is_wakeup_from_sleep(void)
{
    return s_wakeup_from_sleep;
}

esp_sleep_source_t board_power_mgmt_get_wakeup_cause(void)
{
    return s_wakeup_cause;
}

bool board_power_mgmt_is_wakeup_from_ble(void)
{
    return s_wakeup_cause == ESP_SLEEP_WAKEUP_UART;
}

bool board_power_mgmt_is_wakeup_from_gpio(void)
{
    return s_wakeup_cause == ESP_SLEEP_WAKEUP_GPIO;
}

/* ================== 电源状态机_tick ================== */

void board_power_mgmt_tick(void)
{
    if (!s_initialized) {
        return;
    }

    // 检查是否在充电（充电时不进入睡眠）
    // 注意：此检测会增加功耗，如不需要可移除
    // if (board_battery_is_charging()) {
    //     if (s_current_state != POWER_STATE_ACTIVE) {
    //         board_power_mgmt_enter_state(POWER_STATE_ACTIVE);
    //     }
    //     return;
    // }

    uint32_t now = board_time_ms();
    uint32_t idle_time = now - s_last_activity_time;

    // 状态机决策
    if (idle_time >= POWER_LIGHT_SLEEP_TIMEOUT_MS) {
        // 进入 Light Sleep（不会返回，唤醒后重启）
        if (s_current_state != POWER_STATE_LIGHT_SLEEP) {
            ESP_LOGI(TAG, "Idle for %dms, entering LIGHT_SLEEP", idle_time);
            board_power_mgmt_enter_state(POWER_STATE_LIGHT_SLEEP);
        }
    } else if (idle_time >= POWER_BLACK_SCREEN_TIMEOUT_MS) {
        // 进入黑屏
        if (s_current_state != POWER_STATE_BLACK_SCREEN && 
            s_current_state != POWER_STATE_LIGHT_SLEEP) {
            ESP_LOGI(TAG, "Idle for %dms, entering BLACK_SCREEN", idle_time);
            board_power_mgmt_enter_state(POWER_STATE_BLACK_SCREEN);
        }
    } else {
        // 活跃状态
        if (s_current_state != POWER_STATE_ACTIVE) {
            ESP_LOGI(TAG, "Activity detected, waking up to ACTIVE");
            board_power_mgmt_enter_state(POWER_STATE_ACTIVE);
        }
    }
}

/* ================== Light Sleep 底层实现 ================== */

void board_power_mgmt_configure_gpio_wakeup(void)
{
    // 配置 4 个按键为唤醒源（低电平触发）
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_LOW_LEVEL,
        .pin_bit_mask = (1ULL << BOARD_GPIO_KEY_UP) |
                        (1ULL << BOARD_GPIO_KEY_DOWN) |
                        (1ULL << BOARD_GPIO_KEY_ENTER) |
                        (1ULL << BOARD_GPIO_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    
    gpio_config(&io_conf);
    
    // 配置 GPIO 唤醒
    esp_sleep_enable_gpio_wakeup();
    
    ESP_LOGI(TAG, "GPIO wakeup configured (4 buttons)");
}

void board_power_mgmt_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "Preparing to enter Light Sleep...");
    
    // 1. 确保显示已关闭
    board_display_set_contrast(0);
    board_leds_off();
    
    // 2. 配置 GPIO 唤醒
    board_power_mgmt_configure_gpio_wakeup();
    
    // 3. 配置 UART 唤醒（用于 BLE 事件）
    // ESP32-C3 的 UART0 可用作唤醒源
    esp_sleep_enable_uart_wakeup(0);
    
    // 4. 配置唤醒后时钟源
    // 使用内部 RC 振荡器快速唤醒（约 5ms）
    
    // 5. 设置 RTC 定时器备份唤醒（安全网，防止无法唤醒）
    // 10 分钟后自动唤醒
    esp_sleep_enable_timer_wakeup(PD_MS_TO_S(600000));
    
    // 6. 标记睡眠状态
    s_wakeup_from_sleep = true;
    s_current_state = POWER_STATE_LIGHT_SLEEP;
    
    // 7. 进入 Light Sleep（不会返回，唤醒后从 app_main 继续）
    ESP_LOGI(TAG, "Entering Light Sleep now...");
    esp_light_sleep_start();
    
    // ← 唤醒后从此处继续执行
    ESP_LOGI(TAG, "Woke up from Light Sleep");
}
