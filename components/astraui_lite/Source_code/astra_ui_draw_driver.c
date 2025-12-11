//
// Created by Fir on 24-11-30.
//

#include "astra_ui_draw_driver.h"
#include "esp_log.h"

static const char* TAG = "AstraUI_Driver";

/* 此处自行编写oled及图形库初始化函数所需的函数 */
// u8g2_t u8g2;  // 这个将在main.c中定义，这里只是外部声明

// uint8_t u8x8_byte_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
// {
//   uint8_t *p = (uint8_t *) arg_ptr;
//   switch (msg)
//   {
//     case U8X8_MSG_BYTE_SEND:
//       for (int i = 0; i < arg_int; i++)
//         HAL_SPI_Transmit(&hspi2,
//                          (const uint8_t *) (p + i),
//                          1,
//                          1000);
//       break;
//     case U8X8_MSG_BYTE_SET_DC:
//       if (arg_int) HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
//       else
//         HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
//       break;
//     case U8X8_MSG_BYTE_START_TRANSFER: break;
//     case U8X8_MSG_BYTE_END_TRANSFER: break;
//     default: return 0;
//   }
//   return 1;
// }

// uint8_t u8x8_stm32_gpio_and_delay(U8X8_UNUSED u8x8_t *u8x8,
//                                   U8X8_UNUSED uint8_t msg, U8X8_UNUSED uint8_t arg_int,
//                                   U8X8_UNUSED void *arg_ptr)
// {
//   switch (msg)
//   {
//     case U8X8_MSG_GPIO_AND_DELAY_INIT: break;
//     case U8X8_MSG_DELAY_MILLI: HAL_Delay(arg_int);
//       break;
//     case U8X8_MSG_GPIO_CS: break;
//     case U8X8_MSG_GPIO_DC:
//     case U8X8_MSG_GPIO_RESET:
//     default: break;
//   }
//   return 1;
// }

// void _ssd1306_transmit_cmd(unsigned char _cmd)
// {
//   //NOLINT
//   HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
//   HAL_SPI_Transmit_DMA(&hspi2, &_cmd, 1);
// }

// void _ssd1306_transmit_data(unsigned char _data, unsigned char _mode)
// {
//   //NOLINT
//   if (!_mode) _data = ~_data;
//   HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
//   HAL_SPI_Transmit_DMA(&hspi2, &_data, 1);
// }

// void _ssd1306_set_cursor(unsigned char _x, unsigned char _y)
// {
//   _ssd1306_transmit_cmd(0xB0 | _y);
//   _ssd1306_transmit_cmd(0x10 | ((_x & 0xF0) >> 4));
//   _ssd1306_transmit_cmd(0x00 | (_x & 0x0F));
// }

// void _ssd1306_fill(unsigned char _data)
// {
//   uint8_t k, n;
//   for (k = 0; k < 8; k++)
//   {
//     _ssd1306_transmit_cmd(0xb0 + k);
//     _ssd1306_transmit_cmd(0x00);
//     _ssd1306_transmit_cmd(0x10);
//     for (n = 0; n < 128; n++)
//       _ssd1306_transmit_data(_data, 1);
//   }
// }

void oled_init()
{
  // OLED初始化已经在main.c中通过u8g2完成，这里不需要额外操作
  ESP_LOGI(TAG, "OLED初始化已通过u8g2完成");
}

void u8g2_init(u8g2_t *u8g2)
{
  // u8g2初始化已经在main.c中完成，这里不需要额外操作
  ESP_LOGI(TAG, "u8g2初始化已在main.c中完成");
  
  // 设置一些默认参数
  u8g2_ClearBuffer(u8g2);
  u8g2_SetFontMode(u8g2, 1);  // 透明字体模式
  u8g2_SetFontDirection(u8g2, 0);  // 正常方向
  u8g2_SetDrawColor(u8g2, 1);  // 设置绘制颜色为白色
}

/* 此处自行编写oled及图形库初始化函数所需的函数 */

/* 此处自行修改内部函数名 */
void astra_ui_driver_init()
{
  ESP_LOGI(TAG, "初始化AstraUI驱动层...");
  
  oled_init();
  u8g2_init(&u8g2);
  
  ESP_LOGI(TAG, "AstraUI驱动层初始化完成");
}

/* 此处自行修改内部函数名 */
