#include "board_hal.h"
#include "esp_log.h"


i2c_master_bus_handle_t board_i2c_bus_handle = NULL;

void board_i2c_init(void) {
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .scl_io_num = BOARD_I2C_SCL_IO,
        .sda_io_num = BOARD_I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &board_i2c_bus_handle));
    
    ESP_LOGI(BOARD_TAG, "I2C initialized");
}
