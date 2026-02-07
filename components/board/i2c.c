#include "board_hal.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


i2c_master_bus_handle_t board_i2c_bus_handle = NULL;

void board_i2c_init(void) {
    // 1) 强制预充电：将 SDA/SCL 暂时配置为推挽高，给没有上拉/没有电阻的设备充电
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << (int)BOARD_I2C_SDA_IO) | (1ULL << (int)BOARD_I2C_SCL_IO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(BOARD_I2C_SDA_IO, 1);
    gpio_set_level(BOARD_I2C_SCL_IO, 1);
    // 静置一小段时间让线路电压稳住
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2) 正式配置并创建 I2C 主机（速度降至 10kHz，增加 glitch 忽略计数）
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL_IO,
        .sda_io_num = BOARD_I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &board_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(BOARD_TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(BOARD_TAG, "I2C initialized (10kHz, glitch_ignore=7)");
    }

    // i2c 驱动会重新配置引脚为开漏并接管，不需要手动 reset 引脚
}
