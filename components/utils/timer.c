#include "timer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "timer";
static bool timer_initialized = false;

esp_err_t timer_init(void)
{
    if (timer_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "初始化系统定时器...");
    
    // ESP-IDF 定时器系统已经在启动时初始化
    timer_initialized = true;
    
    ESP_LOGI(TAG, "系统定时器初始化完成");
    return ESP_OK;
}

uint32_t timer_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void timer_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint64_t timer_get_us(void)
{
    return esp_timer_get_time();
}

void timer_delay_us(uint32_t us)
{
    if (us < 1000) {
        // 小于1ms的使用忙等待
        esp_rom_delay_us(us);
    } else {
        // 大于等于1ms的使用任务延时
        vTaskDelay(pdMS_TO_TICKS(us / 1000));
    }
}