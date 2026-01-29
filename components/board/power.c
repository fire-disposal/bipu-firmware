#include "board.h"
#include "board_private.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 定义参数
#define ADC_UNIT               ADC_UNIT_1
// ADC_CHANNEL 将动态获取
#define ADC_ATTEN              ADC_ATTEN_DB_12 // 0-3.3V 量程
#define VOLTAGE_DIVIDER_RATIO  2.0f  // 1/2 分压

// 全局句柄
static adc_oneshot_unit_handle_t s_adc1_handle;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_do_calibration = false;
static adc_channel_t s_adc_channel;

/**
 * @brief ADC 校准初始化
 */
static bool power_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
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

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    if (calibrated) {
        ESP_LOGI(BOARD_TAG, "ADC calibration initialized");
    } else {
        ESP_LOGW(BOARD_TAG, "ADC calibration failed or not supported");
    }
    return calibrated;
}

void board_power_init(void)
{
    // 1. 获取通道
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BOARD_GPIO_BATTERY, &unit, &s_adc_channel));
    ESP_LOGI(BOARD_TAG, "Battery ADC Channel: %d, Unit: %d", s_adc_channel, unit);

    // 2. 配置 ADC 单元
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &s_adc1_handle));

    // 3. 配置 ADC 通道
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, s_adc_channel, &config));

    // 4. 校准初始化
    s_do_calibration = power_adc_calibration_init(unit, s_adc_channel, ADC_ATTEN, &s_adc_cali_handle);
    
    ESP_LOGI(BOARD_TAG, "Power management initialized");
}

float board_battery_voltage(void)
{
    int adc_raw;
    int voltage_mv;

    if (!s_adc1_handle) {
        return 0.0f;
    }

    // 读取原始值
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc1_handle, s_adc_channel, &adc_raw));
    
    if (s_do_calibration) {
        // 使用校准转换原始值为 mV
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_adc_cali_handle, adc_raw, &voltage_mv));
    } else {
        // 无校准时的简易转换 (假设参考电压 1100mV, 12位精度)
        // 注意：这里的公式可能只是近似值，使用校准最好
        voltage_mv = (adc_raw * 2500) / 4095; // 假设默认参考电压
    }

    // 还原分压前的电压 (mV -> V)
    float actual_voltage = (voltage_mv / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
    return actual_voltage;
}

uint8_t board_battery_percent(void)
{
    float bat_v = board_battery_voltage();
    
    // 简单的电量百分比估算 (以3.0V为0%，4.2V为100%为例)
    // 线性估算仅供参考
    float percentage = (bat_v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    
    return (uint8_t)percentage;
}
