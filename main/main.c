/**
 * @file main.c
 * @brief AstraUI Lite 在 ESP-IDF 环境下的主程序
 * @author Kilo Code
 * @date 2025-12-10
 * 
 * 本程序实现了 AstraUI Lite 与 u8g2_hal_esp_idf 的稳健集成，
 * 采用分层架构设计，确保代码清晰、安全、可维护。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "u8g2.h"
#include "astra_ui_core.h"
#include "astra_ui_item.h"

/*==============================================================================
 * 宏定义和配置
 *============================================================================*/
#define TAG "AstraUI_System"


// 按键配置
#define BUTTON_COUNT         4
#define BUTTON_GPIO_PINS     {GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13}
#define BUTTON_DEBOUNCE_MS   50
#define BUTTON_LONG_PRESS_MS 1500

// 任务配置
#define DISPLAY_TASK_STACK_SIZE  8192
#define DISPLAY_TASK_PRIORITY    5
#define BUTTON_TASK_STACK_SIZE   4096
#define BUTTON_TASK_PRIORITY     3

// 系统配置
#define SYSTEM_INFO_UPDATE_MS    1000
#define UI_REFRESH_INTERVAL_MS   50

/*==============================================================================
 * 类型定义
 *============================================================================*/
typedef struct {
    gpio_num_t gpio_pin;
    bool current_state;
    bool last_state;
    uint32_t press_start_time;
    bool is_pressed;
} button_t;

typedef struct {
    u8g2_t u8g2;
    button_t buttons[BUTTON_COUNT];
    bool system_initialized;
    uint32_t last_info_update;
} system_context_t;

/*==============================================================================
 * 静态变量
 *============================================================================*/
static system_context_t g_system = {0};
static const gpio_num_t button_gpio_pins[BUTTON_COUNT] = BUTTON_GPIO_PINS;

/*==============================================================================
 * 函数前向声明
 *============================================================================*/
static esp_err_t system_hardware_init(void);
static esp_err_t button_system_init(void);
static void button_task(void *pvParameters);
static void display_task(void *pvParameters);
static void process_button_input(void);
static void update_system_info(void);
static void create_ui_menu(void);
static void user_item_init(void);
static void user_item_loop(void);
static void user_item_exit(void);
static void handle_ui_navigation(void) {
    // TODO: 补全导航逻辑
}

// 外部变量声明
extern bool in_astra;

/*==============================================================================
 * 硬件初始化函数
 *============================================================================*/

/**
 * @brief 系统硬件初始化
 * @return ESP_OK 成功，其他值表示失败
 */
static esp_err_t system_hardware_init(void)
{
    ESP_LOGI(TAG, "初始化系统硬件...");
    
    // 完全交由UI框架处理显示初始化
    if (astra_ui_display_init(&g_system.u8g2) != ESP_OK)
        return ESP_FAIL;
    
    if (button_system_init() != ESP_OK)
        return ESP_FAIL;
    g_system.system_initialized = true;
    ESP_LOGI(TAG, "系统硬件初始化完成");
    return ESP_OK;
}


/**
 * @brief 按键系统初始化
 * @return ESP_OK 成功，其他值表示失败
 */
static esp_err_t button_system_init(void)
{
    ESP_LOGI(TAG, "初始化按键系统...");
    
    for (int i = 0; i < BUTTON_COUNT; i++) {
        g_system.buttons[i].gpio_pin = button_gpio_pins[i];
        g_system.buttons[i].current_state = true;  // 上拉，默认高电平
        g_system.buttons[i].last_state = true;
        g_system.buttons[i].is_pressed = false;
        g_system.buttons[i].press_start_time = 0;
        
        // 配置 GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << button_gpio_pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "按键 GPIO %d 配置失败", button_gpio_pins[i]);
            return ret;
        }
    }
    
    ESP_LOGI(TAG, "按键系统初始化完成");
    return ESP_OK;
}

/*==============================================================================
 * UI 相关函数
 *============================================================================*/

/**
 * @brief 创建 UI 菜单结构
 */
static void create_ui_menu(void)
{
    ESP_LOGI(TAG, "创建 UI 菜单...");
    
    // 获取根列表
    astra_list_item_t *root = astra_get_root_list();
    if (!root) {
        ESP_LOGE(TAG, "无法获取根列表");
        return;
    }
    
    // 静态变量用于存储状态
    static bool wifi_switch_value = false;
    static int16_t brightness_value = 50;
    
    // 添加菜单项
    astra_push_item_to_list(root, astra_new_list_item("系统信息", list_icon));
    astra_push_item_to_list(root, astra_new_list_item("OLED状态", list_icon));
    astra_push_item_to_list(root, astra_new_list_item("按键测试", list_icon));
    astra_push_item_to_list(root, astra_new_switch_item("WiFi", &wifi_switch_value, NULL, NULL, switch_icon));
    astra_push_item_to_list(root, astra_new_slider_item("亮度", &brightness_value, 5, 0, 100, NULL, NULL, slider_icon));
    astra_push_item_to_list(root, astra_new_button_item("重启系统", NULL, power_icon));
    astra_push_item_to_list(root, astra_new_user_item("按键状态", user_item_init, user_item_loop, user_item_exit, user_icon));
    
    // 初始化 UI 核心
    astra_init_core();
    
    ESP_LOGI(TAG, "UI 菜单创建完成");
}

/**
 * @brief 用户自定义项初始化
 */
static void user_item_init(void)
{
    ESP_LOGI(TAG, "用户项初始化");
}

/**
 * @brief 用户自定义项主循环
 */
static void user_item_loop(void)
{
    char buf[32];
    
    u8g2_ClearBuffer(&g_system.u8g2);
    u8g2_SetFont(&g_system.u8g2, u8g2_font_6x12_tf);
    u8g2_DrawStr(&g_system.u8g2, 0, 12, "按键状态页面");
    
    // 显示按键状态
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        snprintf(buf, sizeof(buf), "KEY%d: %d", i + 1, g_system.buttons[i].is_pressed);
        u8g2_DrawStr(&g_system.u8g2, 0, 28 + i * 16, buf);
    }
    
    u8g2_SendBuffer(&g_system.u8g2);
}

/**
 * @brief 用户自定义项退出
 */
static void user_item_exit(void)
{
    ESP_LOGI(TAG, "用户项退出");
}

/*==============================================================================
 * 输入处理函数
 *============================================================================*/

/**
 * @brief 处理按键输入
 */
static void process_button_input(void)
{
    static bool long_press_detected = false;
    static uint32_t long_press_start_time = 0;
    
    // 检查是否有按键被按下
    bool any_button_pressed = false;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (g_system.buttons[i].is_pressed) {
            any_button_pressed = true;
            break;
        }
    }
    
    if (any_button_pressed && !long_press_detected) {
        if (long_press_start_time == 0) {
            long_press_start_time = xTaskGetTickCount();
        } else if ((xTaskGetTickCount() - long_press_start_time) > pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
            // 长按检测，进入 AstraUI
            ESP_LOGI(TAG, "长按检测，进入 AstraUI");
            extern bool in_astra;
            in_astra = true;
            long_press_detected = true;
            long_press_start_time = 0;
        }
    } else {
        long_press_detected = false;
        long_press_start_time = 0;
    }
}


/**
 * @brief 更新系统信息
 */
static void update_system_info(void)
{
    uint32_t current_time = xTaskGetTickCount();
    
    if (current_time - g_system.last_info_update > pdMS_TO_TICKS(SYSTEM_INFO_UPDATE_MS)) {
        // 可以在这里更新系统状态信息
        g_system.last_info_update = current_time;
    }
}

/*==============================================================================
 * 任务函数
 *============================================================================*/

/**
 * @brief 按键处理任务
 */
static void button_task(void *pvParameters)
{
    ESP_LOGI(TAG, "按键任务启动");
    
    while (1) {
        // 扫描按键状态
        for (int i = 0; i < BUTTON_COUNT; i++) {
            button_t *btn = &g_system.buttons[i];
            bool current_reading = gpio_get_level(btn->gpio_pin);
            
            // 简单的消抖处理
            if (current_reading != btn->last_state) {
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
                current_reading = gpio_get_level(btn->gpio_pin);
            }
            
            btn->current_state = current_reading;
            btn->is_pressed = !current_reading;  // 低电平表示按下
            
            btn->last_state = current_reading;
        }
        
        // 处理按键输入
        process_button_input();
        handle_ui_navigation();
        
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 扫描间隔
    }
}

/**
 * @brief 显示任务
 */
static void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "显示任务启动");
    
    // 创建 UI 菜单
    create_ui_menu();
    
    // 显示启动信息
    u8g2_ClearBuffer(&g_system.u8g2);
    u8g2_SetFont(&g_system.u8g2, u8g2_font_6x12_tf);
    u8g2_DrawStr(&g_system.u8g2, 0, 12, "AstraUI Lite");
    u8g2_DrawStr(&g_system.u8g2, 0, 28, "系统启动中...");
    u8g2_SendBuffer(&g_system.u8g2);
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // 显示 1 秒启动信息
    
    while (1) {
        // 更新系统信息
        update_system_info();
        
        // 处理 UI 主循环
        extern bool in_astra;
        if (in_astra) {
            astra_ui_main_core();    // UI 主逻辑
            astra_ui_widget_core();  // UI 渲染
        } else {
            // 默认显示界面
            u8g2_ClearBuffer(&g_system.u8g2);
            u8g2_SetFont(&g_system.u8g2, u8g2_font_6x12_tf);
            u8g2_DrawStr(&g_system.u8g2, 0, 12, "长按任意键");
            u8g2_DrawStr(&g_system.u8g2, 0, 28, "进入 AstraUI");
            u8g2_SendBuffer(&g_system.u8g2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS));
    }
}

/*==============================================================================
 * 主函数
 *============================================================================*/

/**
 * @brief 应用程序主入口
 */
void app_main(void)
{
    ESP_LOGI(TAG, "AstraUI Lite ESP-IDF 系统启动");
    if (system_hardware_init() != ESP_OK) return;
    if (xTaskCreate(button_task, "button_task", BUTTON_TASK_STACK_SIZE, NULL, BUTTON_TASK_PRIORITY, NULL) != pdPASS) return;
    if (xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK_SIZE, NULL, DISPLAY_TASK_PRIORITY, NULL) != pdPASS) return;
    ESP_LOGI(TAG, "系统启动完成");
}

