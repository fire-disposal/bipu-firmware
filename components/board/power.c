#include "board.h"
#include "board_hal.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 定义参数
#define ADC_UNIT               ADC_UNIT_1
// ADC_CHANNEL 将动态获取
#define ADC_ATTEN              ADC_ATTEN_DB_12 // 0-3.3V 量程
#define R_DIV_TOP_OHMS        511000.0f // 上分压电阻 (欧姆)
#define R_DIV_BOTTOM_OHMS     511000.0f // 下分压电阻 (欧姆)
// 电压还原系数: Vin = Vout * VOLTAGE_DIVIDER_RATIO
#define VOLTAGE_DIVIDER_RATIO  ((R_DIV_TOP_OHMS + R_DIV_BOTTOM_OHMS) / R_DIV_BOTTOM_OHMS)

// 充电检测参数
// 当平滑电压变化超过阈值时，认为有上升/下降趋势（单位: V）
#define CHARGING_VOLTAGE_THRESHOLD   0.05f  // 电压上升阈值 (50mV)
#define CHARGING_STABLE_COUNT        2      // 连续检测次数，减少判定所需样本以降低延迟
// 降低采样频率以节省功耗与噪声影响
#define BATTERY_CHECK_INTERVAL_MS    10000U // 10秒刷新一次
// 指数平滑系数（EMA）用于滤掉瞬时噪声，范围(0,1], 越小越平滑
#define BATTERY_SMOOTH_ALPHA         0.25f

// 电压安全边界
#define BATTERY_VOLTAGE_MIN          2.5f   // 最小合理电压
#define BATTERY_VOLTAGE_MAX          5.0f   // 最大合理电压 (考虑充电时)

// ADC 采样缓存间隔（无论外部调用频率如何，实际采样不超过此频率）
#define ADC_SAMPLE_INTERVAL_MS       5000U // 5秒采样一次（更频繁检测电压变化）

// 全局句柄
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_do_calibration = false;
static adc_channel_t s_adc_channel;
static bool s_power_initialized = false;

// 充电检测相关变量
static float s_smoothed_voltage = 0.0f;
static float s_prev_smoothed_voltage = 0.0f;
static bool s_smoothed_initialized = false;
static bool s_is_charging = false;
static int s_charging_detect_count = 0;
static uint32_t s_last_voltage_check_time = 0;

// ADC 缓存：避免高频调用导致大量 ADC 采样和日志
static float s_cached_voltage = 0.0f;
static uint32_t s_last_adc_sample_time = 0;
static bool s_voltage_out_of_range = false; // 上次是否处于异常范围（仅状态变化时打日志）

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
    if (s_power_initialized) {
        ESP_LOGW(BOARD_TAG, "Power management already initialized");
        return;
    }

    // 1. 获取通道
    adc_unit_t unit;
    esp_err_t ret = adc_oneshot_io_to_channel(BOARD_GPIO_BATTERY, &unit, &s_adc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to get ADC channel: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(BOARD_TAG, "Battery ADC Channel: %d, Unit: %d", s_adc_channel, unit);

    // 2. 配置 ADC 单元
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = unit,
    };
    ret = adc_oneshot_new_unit(&init_config1, &s_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
        return;
    }

    // 3. 配置 ADC 通道
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(s_adc1_handle, s_adc_channel, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        return;
    }

    // 4. 校准初始化
    s_do_calibration = power_adc_calibration_init(unit, s_adc_channel, ADC_ATTEN, &s_adc_cali_handle);
    // 打印分压信息以便调试（例如：511k/511k）
    ESP_LOGI(BOARD_TAG, "Voltage divider: Rtop=%.0fR Rbot=%.0fR ratio=%.3f", R_DIV_TOP_OHMS, R_DIV_BOTTOM_OHMS, VOLTAGE_DIVIDER_RATIO);
    
    // 5. 初始化充电检测相关变量（使用EMA）
    s_smoothed_voltage = 0.0f;
    s_prev_smoothed_voltage = 0.0f;
    s_smoothed_initialized = false;
    s_is_charging = false;
    s_charging_detect_count = 0;
    s_last_voltage_check_time = 0;
    
    s_power_initialized = true;
    ESP_LOGI(BOARD_TAG, "Power management initialized");
}

float board_battery_voltage(void)
{
    if (!s_power_initialized || s_adc1_handle == NULL) {
        return 0.0f;
    }

    // 限频采样：距上次采样不足间隔时直接返回缓存值
    uint32_t now = board_time_ms();
    if (s_last_adc_sample_time != 0 && (now - s_last_adc_sample_time) < ADC_SAMPLE_INTERVAL_MS) {
        return s_cached_voltage;
    }
    s_last_adc_sample_time = now;

    int adc_raw;
    int voltage_mv;

    // 读取原始值
    esp_err_t ret = adc_oneshot_read(s_adc1_handle, s_adc_channel, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGW(BOARD_TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return s_cached_voltage; // 返回缓存值
    }
    
    if (s_do_calibration && s_adc_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(s_adc_cali_handle, adc_raw, &voltage_mv);
        if (ret != ESP_OK) {
            voltage_mv = (adc_raw * 1100) / 4095;
        }
    } else {
        voltage_mv = (adc_raw * 1100) / 4095;
    }

    // 还原分压前的电压 (mV -> V)
    float actual_voltage = (voltage_mv / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
    
    // 安全边界检查（仅在状态变化时打印，避免刷屏）
    bool out_of_range = (actual_voltage < BATTERY_VOLTAGE_MIN || actual_voltage > BATTERY_VOLTAGE_MAX);
    if (out_of_range && !s_voltage_out_of_range) {
        // 从正常→异常，打一次日志
        if (actual_voltage < BATTERY_VOLTAGE_MIN) {
            ESP_LOGW(BOARD_TAG, "Battery voltage out of range: %.2fV (pin floating or no battery?)", actual_voltage);
        } else {
            ESP_LOGW(BOARD_TAG, "Battery voltage abnormally high: %.2fV", actual_voltage);
        }
    } else if (!out_of_range && s_voltage_out_of_range) {
        // 从异常→正常，打一次恢复日志
        ESP_LOGI(BOARD_TAG, "Battery voltage recovered: %.2fV", actual_voltage);
    }
    s_voltage_out_of_range = out_of_range;

    // 钳位到安全范围
    if (actual_voltage < BATTERY_VOLTAGE_MIN) actual_voltage = BATTERY_VOLTAGE_MIN;
    if (actual_voltage > BATTERY_VOLTAGE_MAX) actual_voltage = BATTERY_VOLTAGE_MAX;
    
    s_cached_voltage = actual_voltage;
    return actual_voltage;
}

uint8_t board_battery_percent(void)
{
    // 缓存电量百分比（跟随 ADC 采样频率自动更新）
    static uint8_t s_cached_pct = 0;
    static uint32_t s_last_pct_time = 0;
    uint32_t now = board_time_ms();
    if (s_last_pct_time != 0 && (now - s_last_pct_time) < ADC_SAMPLE_INTERVAL_MS) {
        return s_cached_pct;
    }
    s_last_pct_time = now;
    
    float bat_v = board_battery_voltage();
    float percentage = (bat_v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    
    s_cached_pct = (uint8_t)percentage;
    return s_cached_pct;
}

/**
 * @brief 更新电压历史记录并检测充电状态
 * @param current_voltage 当前电压值
 */
static void power_update_charging_detection(float current_voltage)
{
    uint32_t current_time = board_time_ms();
    
    // 限制检测频率，避免过于频繁的检测
    if (current_time - s_last_voltage_check_time < BATTERY_CHECK_INTERVAL_MS) {
        return;
    }
    s_last_voltage_check_time = current_time;
    // EMA 平滑
    if (!s_smoothed_initialized) {
        s_smoothed_voltage = current_voltage;
        s_prev_smoothed_voltage = current_voltage;
        s_smoothed_initialized = true;
        return;
    }

    s_prev_smoothed_voltage = s_smoothed_voltage;
    s_smoothed_voltage = (1.0f - BATTERY_SMOOTH_ALPHA) * s_smoothed_voltage + BATTERY_SMOOTH_ALPHA * current_voltage;

    float diff = s_smoothed_voltage - s_prev_smoothed_voltage;

    if (diff > CHARGING_VOLTAGE_THRESHOLD) {
        s_charging_detect_count++;
        if (s_charging_detect_count >= CHARGING_STABLE_COUNT) {
            s_is_charging = true;
        }
    } else if (diff < -CHARGING_VOLTAGE_THRESHOLD) {
        if (s_charging_detect_count > 0) s_charging_detect_count--;
        if (s_charging_detect_count == 0) s_is_charging = false;
    } else {
        // 无明显变化，逐步衰减计数以减少误判保留
        if (s_charging_detect_count > 0) s_charging_detect_count--;
        if (s_charging_detect_count == 0) s_is_charging = false;
    }
}

bool board_battery_is_charging(void)
{
    // 充电状态缓存：跟随充电检测间隔自动更新
    static uint32_t s_last_charge_check = 0;
    uint32_t now = board_time_ms();
    if (s_last_charge_check != 0 && (now - s_last_charge_check) < BATTERY_CHECK_INTERVAL_MS) {
        return s_is_charging; // 返回缓存状态
    }
    s_last_charge_check = now;
    
    float current_voltage = board_battery_voltage();
    power_update_charging_detection(current_voltage);
    
    return s_is_charging;
}
