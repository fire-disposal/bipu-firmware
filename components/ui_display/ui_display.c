#include "ui_display.h"
#include "ui_content.h"
#include "ui_input.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

// 外部依赖：AstraUI
#include "astra_ui_draw_driver.h"
#include "astra_ui_core.h"

#define I2C_MASTER_SCL_IO           GPIO_NUM_1      // SCL引脚
#define I2C_MASTER_SDA_IO           GPIO_NUM_2      // SDA引脚
#define I2C_MASTER_FREQ_HZ          400000           // I2C时钟频率
#define OLED_I2C_ADDRESS            0x3C      

static const char* TAG = "ui_display";
static i2c_port_t s_i2c_port = I2C_NUM_0;

// 全局 u8g2 实例，供 AstraUI 驱动层使用
u8g2_t u8g2;

// 字体定义（可调：越小越紧凑）
const void* astra_font = u8g2_font_wqy12_t_chinese3;   // 中文字体 12px
const void* astra_font_en = u8g2_font_5x7_tf;          // 英文字体 5x7

/**
 * @brief ESP-IDF原生I2C字节传输回调函数
 */
static uint8_t esp_idf_i2c_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    static uint8_t buffer[128];
    static uint8_t buf_idx = 0;
    esp_err_t ret;

    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;

        case U8X8_MSG_BYTE_SEND:
            {
                uint8_t* data = (uint8_t*)arg_ptr;
                for (int i = 0; i < arg_int; i++) {
                    if (buf_idx < sizeof(buffer)) {
                        buffer[buf_idx++] = data[i];
                    }
                }
            }
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            {
                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
                i2c_master_write(cmd, buffer, buf_idx, true);
                i2c_master_stop(cmd);
                ret = i2c_master_cmd_begin(s_i2c_port, cmd, pdMS_TO_TICKS(100));
                i2c_cmd_link_delete(cmd);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "I2C传输失败: %d", ret);
                    return 0;
                }
            }
            break;

        default:
            return 0;
    }

    return 1;
}

/** 
 * @brief ESP-IDF原生GPIO和延时回调函数
 */
static uint8_t esp_idf_gpio_and_delay_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            // GPIO初始化在I2C配置中完成
            break;
            
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;
            
        case U8X8_MSG_DELAY_10MICRO:
            esp_rom_delay_us(10 * arg_int);
            break;
            
        default:
            break;
    }
    
    return 1;
}

/**
 * @brief 初始化I2C总线
 */
static esp_err_t i2c_master_init(void)
{
    ESP_LOGI(TAG, "初始化I2C总线: SDA=%d, SCL=%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(s_i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C参数配置失败: %d", ret);
        return ret;
    }
    ret = i2c_driver_install(s_i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C驱动安装失败: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "I2C总线初始化成功");
    return ESP_OK;
}

// 显示初始化任务（避免阻塞 app_main）
static void ui_display_init_task(void* arg)
{
    ESP_LOGI(TAG, "显示系统初始化任务开始...");

    // 初始化I2C总线
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C初始化失败");
        goto end;
    }

    // 使用u8g2内置SSD1309 I2C驱动
    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(
        &u8g2,
        U8G2_R0,
        esp_idf_i2c_byte_cb,
        esp_idf_gpio_and_delay_cb
    );

    // 执行显示初始化
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    // 初始化AstraUI驱动层
    ESP_LOGI(TAG, "初始化AstraUI驱动层...");
    astra_ui_driver_init();
    ESP_LOGI(TAG, "AstraUI驱动层初始化完成");

    ESP_LOGI(TAG, "OLED显示初始化完成");

end:
    vTaskDelete(NULL);  // 任务结束，自我删除
}

esp_err_t ui_display_init(void)
{
    // 创建显示初始化任务，避免阻塞主任务
    xTaskCreate(ui_display_init_task, "ui_display_init", 4096, NULL, 5, NULL);
    return ESP_OK;
}

void ui_display_hello_world(void)
{
    // 清除缓冲区
    u8g2_ClearBuffer(&u8g2);
    
    // 设置字体
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    
    // 绘制字符串
    u8g2_DrawStr(&u8g2, 10, 30, "Hello World!");
    u8g2_DrawStr(&u8g2, 10, 45, "bipupu!");
    
    // 发送缓冲区到显示
    u8g2_SendBuffer(&u8g2);
    
    ESP_LOGI(TAG, "Hello World 显示完成");
}

u8g2_t* ui_display_get_u8g2(void)
{
    return &u8g2;
}

// UI 渲染任务（独立任务，避免阻塞主循环）
void ui_render_task(void* arg)
{
    while (1) {
        ui_display_main_loop();
        vTaskDelay(pdMS_TO_TICKS(16));  // 约 60 FPS
    }
}

void ui_display_main_loop(void)
{
    ad_astra(); // 检查是否进入AstraUI
    u8g2_ClearBuffer(&u8g2);
    astra_ui_main_core(); // AstraUI主渲染逻辑
    u8g2_SendBuffer(&u8g2); // 刷新显示缓冲区，确保AstraUI内容显示
}

bool ui_display_start(void)
{
    // 初始化显示系统（异步任务）
    esp_err_t ret = ui_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "显示系统初始化失败");
        return false;
    }

    // 等待显示初始化完成（简单延时，确保任务完成）
    vTaskDelay(pdMS_TO_TICKS(500));

    // 显示Hello World
    ui_display_hello_world(); 

    // 延时2秒
    vTaskDelay(pdMS_TO_TICKS(2000));

    // UI 内容初始化：创建并注册所有 UI 项
    ui_content_init();

    // 初始化按键输入（GPIO 10/11/12/13，拉低触发）
    ui_input_init();

    return true;
}