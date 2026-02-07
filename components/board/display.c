#include "board.h"
#include "board_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static u8g2_t s_u8g2;
static i2c_master_dev_handle_t display_dev_handle = NULL;

/* ================== u8g2 回调函数 ================== */
static uint8_t u8g2_esp32_i2c_byte_cb(u8x8_t *u8x8, uint8_t msg,
                                      uint8_t arg_int, void *arg_ptr) {
  static uint8_t buffer[128];
  static uint8_t buf_idx = 0;

  switch (msg) {
  case U8X8_MSG_BYTE_START_TRANSFER:
    buf_idx = 0;
    break;

  case U8X8_MSG_BYTE_SEND: {
    uint8_t *data = (uint8_t *)arg_ptr;
    for (int i = 0; i < arg_int && buf_idx < sizeof(buffer); i++) {
      buffer[buf_idx++] = data[i];
    }
    break;
  }

  case U8X8_MSG_BYTE_END_TRANSFER: {
    if (display_dev_handle == NULL) return 0;
    
    esp_err_t ret = i2c_master_transmit(display_dev_handle, buffer, buf_idx, -1);

    if (ret != ESP_OK) {
      ESP_LOGE(BOARD_TAG, "I2C transfer failed: %d", ret);
      return 0;
    }
    break;
  }

  default:
    break;
  }

  return 1;
}

static uint8_t u8g2_esp32_gpio_delay_cb(u8x8_t *u8x8, uint8_t msg,
                                        uint8_t arg_int, void *arg_ptr) {
  switch (msg) {
  case U8X8_MSG_DELAY_MILLI:
    vTaskDelay(pdMS_TO_TICKS(arg_int));
    break;

  case U8X8_MSG_DELAY_10MICRO:
    ets_delay_us(10 * arg_int);
    break;

  default:
    break;
  }
  return 1;
}

void board_display_init(void) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_OLED_I2C_ADDRESS,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(board_i2c_bus_handle, &dev_cfg, &display_dev_handle));

    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(
      &s_u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_delay_cb);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SendBuffer(&s_u8g2);
    
    ESP_LOGI(BOARD_TAG, "Display initialized");
}

/* ================== 显示接口实现 ================== */
void board_display_begin(void) { u8g2_ClearBuffer(&s_u8g2); }

void board_display_end(void) { u8g2_SendBuffer(&s_u8g2); }

void board_display_text(int x, int y, const char *text) {
  if (text) {
    u8g2_SetFont(&s_u8g2, u8g2_font_wqy12_t_gb2312a);
    u8g2_DrawUTF8(&s_u8g2, x, y, text);
  }
}

void board_display_glyph(int x, int y, uint16_t encoding) {
  u8g2_DrawGlyph(&s_u8g2, x, y, encoding);
}

void board_display_set_font(const void* font) {
  if (font) {
    u8g2_SetFont(&s_u8g2, (const uint8_t*)font);
  }
}

void board_display_rect(int x, int y, int w, int h, bool fill) {
  if (fill) {
    u8g2_DrawBox(&s_u8g2, x, y, w, h);
  } else {
    u8g2_DrawFrame(&s_u8g2, x, y, w, h);
  }
}

int board_display_text_width(const char* text) {
  if (!text) return 0;
  return (int)u8g2_GetUTF8Width(&s_u8g2, (char*)text);
}
