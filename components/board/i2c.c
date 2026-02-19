#include "board_pins.h"      // GPIO引脚定义
#include "board.h"  // I2C总线句柄定义 and helpers
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

esp_err_t board_i2c_init(void) {
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
        return ret;
    } else {
        ESP_LOGI(BOARD_TAG, "I2C master bus initialized successfully (SCL=%d, SDA=%d)",
                 BOARD_I2C_SCL_IO, BOARD_I2C_SDA_IO);
    }
    return ESP_OK;
}

// Chunked transmit helper: send `len` bytes in `chunk_size` pieces with up to 3 attempts per chunk.
esp_err_t board_i2c_transmit_chunked(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, size_t chunk_size, TickType_t timeout)
{
    if (dev == NULL || data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    size_t off = 0;
    while (off < len) {
        size_t chunk = (len - off) > chunk_size ? chunk_size : (len - off);
        esp_err_t tret = ESP_FAIL;
        for (int attempt = 0; attempt < 3; attempt++) {
            tret = i2c_master_transmit(dev, &data[off], chunk, timeout);
            if (tret == ESP_OK) break;
            ESP_LOGD(BOARD_TAG, "i2c chunk attempt %d failed: %s (%d) addr=0x%02x off=%zu chunk=%zu",
                     attempt + 1, esp_err_to_name(tret), tret, BOARD_OLED_I2C_ADDRESS, off, chunk);
            /* 避免长期阻塞：使用短微秒延迟替代毫秒级 vTaskDelay，以减少整体传输延迟
               原先的 5ms 在高频刷新场景下会显著拉慢帧率；此处使用 200us 的短延时
               以便让总线恢复同时不引入太大开销。 */
            ets_delay_us(200);
        }
        if (tret != ESP_OK) {
            ESP_LOGW(BOARD_TAG, "i2c chunk failed after retries: %s (%d) addr=0x%02x off=%zu chunk=%zu",
                     esp_err_to_name(tret), tret, BOARD_OLED_I2C_ADDRESS, off, chunk);
            return tret;
        }
        off += chunk;
    }
    return ESP_OK;
}
