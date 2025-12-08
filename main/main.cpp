#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <u8g2.h>

#include "sdkconfig.h"
#include "u8g2_esp32_hal.h"

// SDA - GPIO20
#define PIN_SDA GPIO_NUM_20

// SCL - GPIO21
#define PIN_SCL GPIO_NUM_21
static char tag[] = "test_ST7567";

void task_test_ST7567(void* ignore) {
  u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
  u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
  u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
  u8g2_esp32_hal_init(u8g2_esp32_hal);

  u8g2_t u8g2;  // a structure which will contain all the data for one display
  u8g2_Setup_st7567_i2c_64x32_f(
      &u8g2, U8G2_R0,
      u8g2_esp32_i2c_byte_cb,
      u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure
  
  // Set I2C address for ST7567 (typically 0x3F or 0x3C)
  u8x8_SetI2CAddress(&u8g2.u8x8, 0x3F);

  ESP_LOGI(tag, "u8g2_InitDisplay");
  u8g2_InitDisplay(&u8g2);  // send init sequence to the display, display is in
                            // sleep mode after this,

  ESP_LOGI(tag, "u8g2_SetPowerSave");
  u8g2_SetPowerSave(&u8g2, 0);  // wake up display
  
  ESP_LOGI(tag, "u8g2_ClearBuffer");
  u8g2_ClearBuffer(&u8g2);
  
  ESP_LOGI(tag, "u8g2_DrawBox");
  u8g2_DrawBox(&u8g2, 0, 26, 80, 6);
  u8g2_DrawFrame(&u8g2, 0, 26, 100, 6);

  ESP_LOGI(tag, "u8g2_SetFont");
  u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
  
  ESP_LOGI(tag, "u8g2_DrawStr");
  u8g2_DrawStr(&u8g2, 2, 17, "ST7567 I2C!");

  ESP_LOGI(tag, "u8g2_SendBuffer");
  u8g2_SendBuffer(&u8g2);

  ESP_LOGI(tag, "All done!");

  vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    // Create the test task
    xTaskCreate(task_test_ST7567, "task_test_ST7567", 4096, NULL, 5, NULL);
}

