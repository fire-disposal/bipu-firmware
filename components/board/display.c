#include "board.h"  // I2C总线句柄
#include "board_pins.h"      // I2C配置常量
#include "board.h"           // 公共接口（包含节能管理）
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "u8g2.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static u8g2_t s_u8g2;
static i2c_master_dev_handle_t display_dev_handle = NULL;
static bool s_display_initialized = false;
static SemaphoreHandle_t s_display_mutex = NULL;

// 显示互斥锁操作
static inline bool display_lock(void) {
    if (s_display_mutex == NULL) return true;
    return xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(500)) == pdTRUE;
}

static inline void display_unlock(void) {
    if (s_display_mutex != NULL) {
        xSemaphoreGive(s_display_mutex);
    }
}

/* ================== u8g2 回调函数 ================== */
static uint8_t u8g2_esp32_i2c_byte_cb(u8x8_t *u8x8, uint8_t msg,
                                      uint8_t arg_int, void *arg_ptr) {
  // 增大缓冲以减少分段频率，同时实现分段发送以避免丢字节
  #define I2C_TX_BUFFER_SIZE 256
  #define I2C_TX_CHUNK_SIZE 64
  static uint8_t buffer[I2C_TX_BUFFER_SIZE];
  static size_t buf_idx = 0;

  switch (msg) {
  case U8X8_MSG_BYTE_START_TRANSFER:
    buf_idx = 0;
    break;

  case U8X8_MSG_BYTE_SEND: {
    uint8_t *data = (uint8_t *)arg_ptr;
    for (int i = 0; i < arg_int; i++) {
      // 如果缓冲即将写满，先发送已有数据再继续累加（避免丢弃多余字节）
      if (buf_idx >= sizeof(buffer)) {
        // 触发发送当前缓冲（同步于 I2C 设备句柄）
        if (display_dev_handle != NULL) {
          // 使用总线层的 chunked helper 一次发送整个缓冲
          esp_err_t tret = board_i2c_transmit_chunked(display_dev_handle, buffer, buf_idx, I2C_TX_CHUNK_SIZE, pdMS_TO_TICKS(200));
          if (tret != ESP_OK) {
            ESP_LOGW(BOARD_TAG, "I2C chunk transmit failed during SEND: %s (%d) addr=0x%02x len=%zu",
                     esp_err_to_name(tret), tret, BOARD_OLED_I2C_ADDRESS, buf_idx);
          }
        }
        buf_idx = 0;
      }
      buffer[buf_idx++] = data[i];
    }
    break;
  }

  case U8X8_MSG_BYTE_END_TRANSFER: {
    if (display_dev_handle == NULL) return 0;

    // 使用总线层 helper 发送整个缓冲
    esp_err_t tret = board_i2c_transmit_chunked(display_dev_handle, buffer, buf_idx, I2C_TX_CHUNK_SIZE, pdMS_TO_TICKS(200));
    if (tret != ESP_OK) {
      ESP_LOGE(BOARD_TAG, "I2C chunk transfer failed: %s (%d) addr=0x%02x len=%zu",
               esp_err_to_name(tret), tret, BOARD_OLED_I2C_ADDRESS, buf_idx);
      // 尝试总线重置一次以恢复，等待时间从 100ms 缩短到 10ms 减少阻塞
      i2c_master_bus_reset(board_i2c_bus_handle);
      vTaskDelay(pdMS_TO_TICKS(10));
      return 0;
    }
    buf_idx = 0;
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
    /* 减少不必要的长阻塞：对于小于系统节拍的毫秒数使用精确微秒延时，
       否则使用 vTaskDelay 以允许任务切换。 */
    if (arg_int < portTICK_PERIOD_MS) {
      ets_delay_us(arg_int * 1000);
    } else {
      vTaskDelay(pdMS_TO_TICKS(arg_int));
    }
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
    if (s_display_initialized) {
        ESP_LOGW(BOARD_TAG, "Display already initialized");
        return;
    }

    // 不再根据供电方式做特殊等待，恢复简单初始化流程

    // 创建显示互斥锁
    s_display_mutex = xSemaphoreCreateMutex();
    if (s_display_mutex == NULL) {
        ESP_LOGE(BOARD_TAG, "Failed to create display mutex");
        // 继续初始化，但无线程安全保护
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_OLED_I2C_ADDRESS,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(board_i2c_bus_handle, &dev_cfg, &display_dev_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(BOARD_TAG, "Failed to add I2C display device: %s", esp_err_to_name(ret));
      return;
    }

    // 不再使用节能模块的保守 I2C 重配置；保持默认设备配置

    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(
      &s_u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_delay_cb);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SendBuffer(&s_u8g2);
    
    s_display_initialized = true;
    ESP_LOGI(BOARD_TAG, "Display initialized successfully");
}

/* ================== 显示接口实现 ================== */
void board_display_begin(void) {
    if (!s_display_initialized) {
        ESP_LOGW(BOARD_TAG, "Display not initialized");
        return;
    }
    if (!display_lock()) {
        ESP_LOGW(BOARD_TAG, "Failed to lock display for begin");
        return;
    }
    u8g2_ClearBuffer(&s_u8g2);
    // 每帧开始时强制重置绘制状态，防止上一帧遗留的 DrawColor/FontMode 污染
    u8g2_SetDrawColor(&s_u8g2, 1);   // 默认白色（前景色）
    u8g2_SetFontMode(&s_u8g2, 0);    // 默认实心模式
}

void board_display_end(void) {
    if (!s_display_initialized) {
        return;
    }
    u8g2_SendBuffer(&s_u8g2);
    display_unlock();
}

void board_display_text(int x, int y, const char *text) {
  if (!s_display_initialized || text == NULL) {
    return;
  }
  // 使用当前已设置的字体绘制，不再强制覆盖
  u8g2_DrawUTF8(&s_u8g2, x, y, text);
}

void board_display_glyph(int x, int y, uint16_t encoding) {
  if (!s_display_initialized) return;
  u8g2_DrawGlyph(&s_u8g2, x, y, encoding);
}

void board_display_set_font(const void* font) {
  if (!s_display_initialized || font == NULL) {
    return;
  }
  u8g2_SetFont(&s_u8g2, (const uint8_t*)font);
}

void board_display_rect(int x, int y, int w, int h, bool fill) {
  if (!s_display_initialized) return;
  // 边界检查
  if (w <= 0 || h <= 0) return;
  
  if (fill) {
    u8g2_DrawBox(&s_u8g2, x, y, w, h);
  } else {
    u8g2_DrawFrame(&s_u8g2, x, y, w, h);
  }
}

int board_display_text_width(const char* text) {
  if (!s_display_initialized || text == NULL) return 0;
  return (int)u8g2_GetUTF8Width(&s_u8g2, (char*)text);
}

void board_display_set_contrast(uint8_t contrast) {
  if (!s_display_initialized) {
    ESP_LOGW(BOARD_TAG, "Cannot set contrast: display not initialized");
    return;
  }
  
  // 始终允许完整亮度（已移除节能模块限制）
  uint8_t max_brightness = 100;

  if (contrast > max_brightness) {
    contrast = max_brightness;
  }
  
  // SSD1309 contrast 寄存器范围 0x00-0xFF，但低于 ~0x10 时屏幕几乎不可见
  // 将输入 0-255 映射到 0x10-0xFF 的安全范围
  uint8_t safe_val = (uint8_t)(0x10 + ((uint16_t)contrast * (0xFF - 0x10)) / 255);
  u8g2_SetContrast(&s_u8g2, safe_val);
  ESP_LOGD(BOARD_TAG, "Display contrast set to %d (raw=%d)", contrast, safe_val);
}

void board_display_set_draw_color(uint8_t color) {
  if (!s_display_initialized) return;
  u8g2_SetDrawColor(&s_u8g2, color);
}

void board_display_set_font_mode(uint8_t mode) {
  if (!s_display_initialized) return;
  u8g2_SetFontMode(&s_u8g2, mode);
}

/* 检查显示是否已初始化 */
bool board_display_is_initialized(void) {
    return s_display_initialized;
}
