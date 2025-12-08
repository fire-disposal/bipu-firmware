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
#include "astra_ui_core.h"
#include "astra_ui_item.h"
#include "astra_ui_u8g2_config.h"

// SDA - GPIO20
#define PIN_SDA GPIO_NUM_20

// SCL - GPIO21
#define PIN_SCL GPIO_NUM_21

static const char *TAG = "AstraUI_Example";

// 全局变量
static bool switch_value = false;
static int16_t slider_value = 50;

// 用户项目函数声明
void user_item_init(void);
void user_item_loop(void);
void user_item_exit(void);

/**
 * @brief 初始化 astraui_lite 菜单结构
 */
void astra_ui_init_menu(void) {
    ESP_LOGI(TAG, "Initializing AstraUI menu structure");
    
    // 创建根列表
    astra_list_item_t *root_list = astra_get_root_list();
    
    // 添加普通列表项
    astra_push_item_to_list(root_list, astra_new_list_item("系统信息", list_icon));
    astra_push_item_to_list(root_list, astra_new_list_item("设置", list_icon));
    
    // 添加开关项
    astra_push_item_to_list(root_list, 
        astra_new_switch_item("WiFi", &switch_value, NULL, NULL, switch_icon));
    
    // 添加滑块项
    astra_push_item_to_list(root_list,
        astra_new_slider_item("亮度", &slider_value, 5, 0, 100, NULL, NULL, slider_icon));
    
    // 添加按钮项
    astra_push_item_to_list(root_list,
        astra_new_button_item("重启系统", NULL, power_icon));
    
    // 添加用户自定义项
    astra_push_item_to_list(root_list,
        astra_new_user_item("用户界面", user_item_init, user_item_loop, user_item_exit, user_icon));
    
    // 初始化核心
    astra_init_core();
    
    ESP_LOGI(TAG, "AstraUI menu structure initialized");
}

/**
 * @brief 用户项目初始化函数
 */
void user_item_init(void) {
    ESP_LOGI(TAG, "User item initialized");
    // 这里可以添加用户界面的初始化代码
}

/**
 * @brief 用户项目循环函数
 */
void user_item_loop(void) {
    // 这里添加用户界面的主循环逻辑
    // 例如：显示一些自定义内容
    
    u8g2_t *u8g2_ptr = astra_ui_get_u8g2();
    if (u8g2_ptr != NULL) {
        u8g2_ClearBuffer(u8g2_ptr);
        
        // 绘制标题
        u8g2_SetFont(u8g2_ptr, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(u8g2_ptr, 10, 15, "用户界面");
        
        // 绘制一些内容
        u8g2_SetFont(u8g2_ptr, u8g2_font_6x10_tr);
        u8g2_DrawStr(u8g2_ptr, 10, 30, "这是用户自定义界面");
        u8g2_DrawStr(u8g2_ptr, 10, 45, "可以显示任何内容");
        
        u8g2_SendBuffer(u8g2_ptr);
    }
    
    // 简单的延时，避免CPU占用过高
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief 用户项目退出函数
 */
void user_item_exit(void) {
    ESP_LOGI(TAG, "User item exited");
    // 这里可以添加清理代码
}

/**
 * @brief 按键处理任务（模拟）
 */
void button_task(void *pvParameters) {
    ESP_LOGI(TAG, "Button task started");
    
    while (1) {
        // 这里应该添加实际的按键检测逻辑
        // 为了演示，我们使用简单的延时
        
        // 模拟按键按下进入 astraui
        // 在实际应用中，这里应该检测真实的按键输入
        static bool last_button_state = false;
        static bool button_pressed = false;
        
        // 模拟按键逻辑（实际项目中需要连接真实的按键）
        if (!button_pressed) {
            // 模拟长按进入 astraui
            in_astra = true;
            astra_init_list();
            button_pressed = true;
            ESP_LOGI(TAG, "Entering AstraUI mode");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief 主显示任务
 */
void display_task(void *pvParameters) {
    ESP_LOGI(TAG, "Display task started");
    // 仅保留 astraui_lite 主循环和小部件刷新
    astra_ui_driver_init();
    astra_ui_init_menu();

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        astra_ui_main_core();
        astra_ui_widget_core();
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 FPS
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting AstraUI Lite Example");

    // 创建显示任务
    xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);

    // 创建按键处理任务
    xTaskCreate(button_task, "button_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "AstraUI Lite Example started");
}