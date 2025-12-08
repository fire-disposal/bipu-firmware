#include "astra_ui_usage.h"
#include "esp_log.h"
#include "astra_ui_core.h"
#include "astra_ui_item.h"
#include "astra_ui_u8g2_config.h"

static const char *TAG = "AstraUI_Usage";

// 示例变量
static bool wifi_enabled = false;
static int16_t brightness = 50;
static int16_t volume = 75;

// 用户项目函数
static void user_settings_init(void);
static void user_settings_loop(void);
static void user_settings_exit(void);

int astraui_lite_init(u8g2_t *u8g2) {
    if (u8g2 == NULL) {
        ESP_LOGE(TAG, "u8g2 pointer is NULL");
        return -1;
    }
    
    ESP_LOGI(TAG, "Initializing AstraUI Lite");
    
    // 设置 u8g2 实例
    astra_ui_set_u8g2(u8g2);
    
    // 初始化驱动
    astra_ui_driver_init();
    
    ESP_LOGI(TAG, "AstraUI Lite initialized successfully");
    return 0;
}

int astraui_lite_create_demo_menu(void) {
    ESP_LOGI(TAG, "Creating demo menu");
    
    // 获取根列表
    astra_list_item_t *root_list = astra_get_root_list();
    if (root_list == NULL) {
        ESP_LOGE(TAG, "Failed to get root list");
        return -1;
    }
    
    // 添加基本信息项
    astra_push_item_to_list(root_list, astra_new_list_item("系统状态", list_icon));
    astra_push_item_to_list(root_list, astra_new_list_item("设备信息", list_icon));
    
    // 添加开关项 - WiFi
    astra_push_item_to_list(root_list, 
        astra_new_switch_item("WiFi 开关", &wifi_enabled, NULL, NULL, switch_icon));
    
    // 添加滑块项 - 亮度
    astra_push_item_to_list(root_list,
        astra_new_slider_item("屏幕亮度", &brightness, 5, 0, 100, NULL, NULL, slider_icon));
    
    // 添加滑块项 - 音量
    astra_push_item_to_list(root_list,
        astra_new_slider_item("音量", &volume, 1, 0, 100, NULL, NULL, slider_icon));
    
    // 添加按钮项 - 重启
    astra_push_item_to_list(root_list,
        astra_new_button_item("重启设备", NULL, power_icon));
    
    // 添加用户自定义项 - 设置
    astra_push_item_to_list(root_list,
        astra_new_user_item("高级设置", user_settings_init, user_settings_loop, user_settings_exit, user_icon));
    
    // 初始化核心
    astra_init_core();
    
    ESP_LOGI(TAG, "Demo menu created successfully");
    return 0;
}

void astraui_lite_loop(void) {
    // 处理 AstraUI Lite 主逻辑
    astra_ui_main_core();
    
    // 刷新小部件（信息栏、弹窗等）
    astra_ui_widget_core();
}

// 用户设置界面函数
static void user_settings_init(void) {
    ESP_LOGI(TAG, "User settings initialized");
}

static void user_settings_loop(void) {
    u8g2_t *u8g2 = astra_ui_get_u8g2();
    if (u8g2 != NULL) {
        u8g2_ClearBuffer(u8g2);
        
        // 绘制标题
        u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(u8g2, 10, 15, "高级设置");
        
        // 绘制选项
        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        u8g2_DrawStr(u8g2, 10, 30, "1. 网络配置");
        u8g2_DrawStr(u8g2, 10, 45, "2. 系统更新");
        
        u8g2_SendBuffer(u8g2);
    }
}

static void user_settings_exit(void) {
    ESP_LOGI(TAG, "User settings exited");
}