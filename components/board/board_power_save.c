#include "board.h"
#include "esp_log.h"
#include <string.h>

static const char* POWER_SAVE_TAG = "board_power_save";

static board_power_save_config_t s_config = {
    .enable_low_power_mode = false,
    .display_brightness = 100,
    .battery_check_interval = 5000,
    .reduce_log_output = false,
    .i2c_speed_reduction = 1
};

static bool s_initialized = false;
static bool s_power_save_enabled = false;

esp_err_t board_power_save_init(const board_power_save_config_t* config) {
    if (config == NULL) {
        ESP_LOGW(POWER_SAVE_TAG, "使用默认节能配置");
    } else {
        memcpy(&s_config, config, sizeof(board_power_save_config_t));
    }
    
    s_initialized = true;
    s_power_save_enabled = s_config.enable_low_power_mode;
    
    ESP_LOGI(POWER_SAVE_TAG, "节能管理初始化完成，模式: %s", 
             s_power_save_enabled ? "启用" : "禁用");
    
    return ESP_OK;
}

esp_err_t board_power_save_auto_config(bool is_usb_power) {
    if (!s_initialized) {
        // 使用默认配置初始化
        board_power_save_init(NULL);
    }
    
    if (is_usb_power) {
        // USB供电时使用正常配置
        s_config.enable_low_power_mode = false;
        s_config.display_brightness = 100;
        s_config.battery_check_interval = 5000;  // 5秒
        s_config.reduce_log_output = false;
        s_config.i2c_speed_reduction = 1;
        ESP_LOGI(POWER_SAVE_TAG, "USB供电模式：正常配置");
    } else {
        // 电池供电时使用节能配置
        s_config.enable_low_power_mode = true;
        s_config.display_brightness = 70;        // 降低亮度
        s_config.battery_check_interval = 15000; // 15秒
        s_config.reduce_log_output = true;
        s_config.i2c_speed_reduction = 2;        // 降低I2C速度
        ESP_LOGI(POWER_SAVE_TAG, "电池供电模式：节能配置");
    }
    
    s_power_save_enabled = s_config.enable_low_power_mode;
    
    // 应用日志级别调整
    if (s_config.reduce_log_output) {
        esp_log_level_set("*", ESP_LOG_WARN);
        esp_log_level_set(POWER_SAVE_TAG, ESP_LOG_INFO);  // 保持本模块日志可见
    } else {
        esp_log_level_set("*", ESP_LOG_INFO);
    }
    
    return ESP_OK;
}

board_power_save_config_t board_power_save_get_config(void) {
    return s_config;
}

bool board_power_save_is_enabled(void) {
    return s_power_save_enabled;
}

esp_err_t board_power_save_set_mode(bool enable) {
    if (!s_initialized) {
        ESP_LOGE(POWER_SAVE_TAG, "节能管理未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    s_power_save_enabled = enable;
    s_config.enable_low_power_mode = enable;
    
    ESP_LOGI(POWER_SAVE_TAG, "节能模式: %s", enable ? "启用" : "禁用");
    return ESP_OK;
}

uint32_t board_power_save_get_battery_interval(bool is_usb_power) {
    if (s_initialized && s_config.enable_low_power_mode && !is_usb_power) {
        return s_config.battery_check_interval;
    }
    // 使用 power.c 中定义的常量，避免重复
    extern uint32_t board_battery_manager_get_update_interval(bool is_usb_power);
    return board_battery_manager_get_update_interval(is_usb_power);
}

uint32_t board_power_save_get_i2c_freq(uint32_t base_freq_hz, bool is_usb_power) {
    if (s_initialized && s_config.enable_low_power_mode && !is_usb_power) {
        return base_freq_hz / s_config.i2c_speed_reduction;
    }
    return base_freq_hz;
}

uint8_t board_power_save_get_display_brightness(bool is_usb_power) {
    if (s_initialized && s_config.enable_low_power_mode && !is_usb_power) {
        return s_config.display_brightness;
    }
    return 100;  // 默认最大亮度
}