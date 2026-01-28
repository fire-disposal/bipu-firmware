#TODO 将电源管理代码整合

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

static const char *TAG = "BATTERY_MONITOR";

// 定义引脚和参数
#define BOARD_GPIO_BATTERY     GPIO_NUM_7
#define ADC_UNIT               ADC_UNIT_1
#define ADC_CHANNEL            ADC_CHANNEL_7 // GPIO 7 对应 ADC1 通道 7
#define ADC_ATTEN              ADC_ATTEN_DB_12 // 5.5版本建议使用12dB以获得最大量程 (约0-3.3V)

// 电压计算参数
#define VOLTAGE_DIVIDER_RATIO  2.0f  // 1/2 分压，所以乘以 2

// 全局句柄
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;

/**
 * @brief ADC 校准初始化
 */
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "使用曲线拟合校准方案");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    return calibrated;
}

/**
 * @brief 初始化电池采样 ADC
 */
void battery_adc_init(void)
{
    // 1. 配置 ADC 单元
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. 配置 ADC 通道
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    // 3. 校准初始化 (ESP32-S3/C3 等建议开启)
    do_calibration = example_adc_calibration_init(ADC_UNIT, ADC_CHANNEL, ADC_ATTEN, &adc_cali_handle);
}

/**
 * @brief 读取电池电压 (单位: mV)
 */
float get_battery_voltage(void)
{
    int adc_raw;
    int voltage_mv;

    // 读取原始值
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw));
    
    if (do_calibration) {
        // 使用校准转换原始值为 mV
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv));
    } else {
        // 无校准时的简易转换 (假设参考电压 1100mV, 12位精度)
        voltage_mv = (adc_raw * 3300) / 4095;
    }

    // 还原分压前的电压 (mV -> V)
    float actual_voltage = (voltage_mv / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
    return actual_voltage;
}

void app_main(void)
{
    battery_adc_init();

    while (1) {
        float bat_v = get_battery_voltage();
        ESP_LOGI(TAG, "当前电池电压: %.2f V", bat_v);

        // 简单的电量百分比估算 (以3.0V为0%，4.2V为100%为例)
        float percentage = (bat_v - 3.0f) / (4.2f - 3.0f) * 100.0f;
        if (percentage > 100) percentage = 100;
        if (percentage < 0) percentage = 0;

        ESP_LOGI(TAG, "估计电量: %.1f%%", percentage);

        vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒采集一次
    }
}