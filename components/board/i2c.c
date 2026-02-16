#include "board_pins.h"      // GPIO引脚定义
#include "board_internal.h"  // I2C总线句柄定义
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void board_i2c_init(void) {
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
        ESP_LOGE(BOARD_TAG, "I2C master bus initialization failed: %s", esp_err_to_name(ret));
        board_i2c_bus_handle = NULL;
    } else {
        ESP_LOGI(BOARD_TAG, "I2C master bus initialized successfully (SCL=%d, SDA=%d)",
                 BOARD_I2C_SCL_IO, BOARD_I2C_SDA_IO);
    }
}
