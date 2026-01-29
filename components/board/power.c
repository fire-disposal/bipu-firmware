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

// 充电检测参数
#define CHARGING_VOLTAGE_THRESHOLD   0.05f  // 电压上升阈值 (50mV)
#define CHARGING_STABLE_COUNT        3      // 连续检测次数
#define VOLTAGE_HISTORY_SIZE         10     // 电压历史记录大小

// 全局句柄
static adc_oneshot_unit_handle_t s_adc1_handle;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_do_calibration = false;
static adc_channel_t s_adc_channel;

// 充电检测相关变量
static float s_voltage_history[VOLTAGE_HISTORY_SIZE];
static int s_voltage_history_index = 0;
static int s_voltage_history_count = 0;
static bool s_is_charging = false;
static int s_charging_detect_count = 0;
static uint32_t s_last_voltage_check_time = 0;

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
    
    // 5. 初始化充电检测相关变量
    for (int i = 0; i < VOLTAGE_HISTORY_SIZE; i++) {
        s_voltage_history[i] = 0.0f;
    }
    s_voltage_history_index = 0;
    s_voltage_history_count = 0;
    s_is_charging = false;
    s_charging_detect_count = 0;
    s_last_voltage_check_time = 0;
    
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

/**
 * @brief 更新电压历史记录并检测充电状态
 * @param current_voltage 当前电压值
 */
static void power_update_charging_detection(float current_voltage)
{
    uint32_t current_time = board_time_ms();
    
    // 限制检测频率，避免过于频繁的检测 (最小间隔1秒)
    if (current_time - s_last_voltage_check_time < 1000) {
        return;
    }
    s_last_voltage_check_time = current_time;
    
    // 记录电压到历史数组
    s_voltage_history[s_voltage_history_index] = current_voltage;
    s_voltage_history_index = (s_voltage_history_index + 1) % VOLTAGE_HISTORY_SIZE;
    if (s_voltage_history_count < VOLTAGE_HISTORY_SIZE) {
        s_voltage_history_count++;
    }
    
    // 需要足够的历史数据才能进行有效判断
    if (s_voltage_history_count < 3) {
        return;
    }
    
    // 计算最近几次电压的平均值
    float recent_sum = 0.0f;
    int recent_count = 0;
    int start_index = s_voltage_history_index - 3;
    if (start_index < 0) start_index += VOLTAGE_HISTORY_SIZE;
    
    for (int i = 0; i < 3; i++) {
        int index = (start_index + i) % VOLTAGE_HISTORY_SIZE;
        recent_sum += s_voltage_history[index];
        recent_count++;
    }
    float recent_avg = recent_sum / recent_count;
    
    // 计算更早几次电压的平均值
    float older_sum = 0.0f;
    int older_count = 0;
    int older_start = start_index - 3;
    if (older_start < 0) older_start += VOLTAGE_HISTORY_SIZE;
    
    for (int i = 0; i < 3; i++) {
        int index = (older_start + i) % VOLTAGE_HISTORY_SIZE;
        if (s_voltage_history[index] > 0.0f) {  // 确保数据有效
            older_sum += s_voltage_history[index];
            older_count++;
        }
    }
    
    if (older_count > 0) {
        float older_avg = older_sum / older_count;
        float voltage_diff = recent_avg - older_avg;
        
        // 检测电压是否持续上升
        if (voltage_diff > CHARGING_VOLTAGE_THRESHOLD) {
            s_charging_detect_count++;
            if (s_charging_detect_count >= CHARGING_STABLE_COUNT) {
                s_is_charging = true;
            }
        } else if (voltage_diff < -CHARGING_VOLTAGE_THRESHOLD) {
            // 电压下降，可能停止充电
            s_charging_detect_count = 0;
            s_is_charging = false;
        } else {
            // 电压稳定，保持当前状态
            // s_charging_detect_count 保持不变
        }
    }
}

bool board_battery_is_charging(void)
{
    // 获取当前电压并更新充电检测
    float current_voltage = board_battery_voltage();
    power_update_charging_detection(current_voltage);
    
    return s_is_charging;
}
