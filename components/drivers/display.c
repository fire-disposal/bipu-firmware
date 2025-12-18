#include "display.h"

#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#include "u8g2.h"

/* ================== 硬件配置 ================== */

#define TAG "display"

#define I2C_MASTER_PORT     I2C_NUM_0
#define I2C_SDA_IO          GPIO_NUM_2
#define I2C_SCL_IO          GPIO_NUM_1
#define I2C_FREQ_HZ         400000

#define OLED_I2C_ADDRESS    0x3C

/* ================== 私有状态 ================== */

static u8g2_t s_u8g2;

static const display_size_t s_display_size = {
    .width  = 128,
    .height = 64,
};

/* ================== u8g2 回调 ================== */

static uint8_t u8g2_esp32_i2c_byte_cb(
    u8x8_t *u8x8,
    uint8_t msg,
    uint8_t arg_int,
    void *arg_ptr)
{
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
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(
            cmd,
            (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE,
            true);
        i2c_master_write(cmd, buffer, buf_idx, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(
            I2C_MASTER_PORT,
            cmd,
            pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C transfer failed: %d", ret);
            return 0;
        }
        break;
    }

    default:
        break;
    }

    return 1;
}

static uint8_t u8g2_esp32_gpio_delay_cb(
    u8x8_t *u8x8,
    uint8_t msg,
    uint8_t arg_int,
    void *arg_ptr)
{
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

/* ================== I2C 初始化 ================== */

static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));
    ESP_ERROR_CHECK(
        i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0));
}

/* ================== 字体映射 ================== */

static const uint8_t *s_font_map[] = {
    [DISPLAY_FONT_SMALL]  = u8g2_font_5x7_tf,
    [DISPLAY_FONT_NORMAL] = u8g2_font_wqy12_t_chinese3,
    [DISPLAY_FONT_LARGE]  = u8g2_font_wqy16_t_chinese3,
};

/* ================== display 接口实现 ================== */

void display_init(void)
{
    ESP_LOGI(TAG, "init display");

    i2c_master_init();

    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(
        &s_u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_delay_cb);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);

    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SendBuffer(&s_u8g2);

    ESP_LOGI(TAG, "display ready");
}

void display_begin_frame(void)
{
    u8g2_ClearBuffer(&s_u8g2);
}

void display_end_frame(void)
{
    u8g2_SendBuffer(&s_u8g2);
}

display_size_t display_get_size(void)
{
    return s_display_size;
}

void display_set_font(display_font_t font)
{
    if (font >= DISPLAY_FONT_SMALL && font <= DISPLAY_FONT_LARGE) {
        u8g2_SetFont(&s_u8g2, s_font_map[font]);
    }
}

void display_draw_text(int x, int y, const char *text)
{
    if (text) {
        u8g2_DrawUTF8(&s_u8g2, x, y, text);
    }
}

void display_draw_rect(int x, int y, int w, int h, bool fill)
{
    if (fill) {
        u8g2_DrawBox(&s_u8g2, x, y, w, h);
    } else {
        u8g2_DrawFrame(&s_u8g2, x, y, w, h);
    }
}

void display_draw_hline(int x, int y, int w)
{
    u8g2_DrawHLine(&s_u8g2, x, y, w);
}

void display_draw_vline(int x, int y, int h)
{
    u8g2_DrawVLine(&s_u8g2, x, y, h);
}

void display_clear(void)
{
    u8g2_ClearBuffer(&s_u8g2);
}
