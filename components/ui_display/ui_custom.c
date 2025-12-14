#include "ui_custom.h"
#include "ui_display.h"
#include "esp_log.h"
#include "u8g2.h"

static const char* TAG = "ui_custom";

// 示例：自定义页面初始化
void my_custom_page_init(void)
{
    ESP_LOGI(TAG, "自定义页面初始化");
}

// 示例：自定义页面主循环
void my_custom_page_loop(void)
{
    u8g2_t* u8g2 = ui_display_get_u8g2();

    // 绘制自定义内容
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 10, 30, "Custom Page");
    u8g2_DrawStr(u8g2, 10, 45, "Hello from custom!");
}

// 示例：自定义页面退出
void my_custom_page_exit(void)
{
    ESP_LOGI(TAG, "自定义页面退出");
}

// 注册自定义页面到 UI 内容
void ui_custom_register_pages(void)
{
    // 这里可以在 ui_content.c 中被调用，添加更多自定义 user_item
    // 例如：
    // astra_list_item_t* item = astra_new_user_item("自定义页", my_custom_page_init, my_custom_page_loop, my_custom_page_exit, user_icon);
    // astra_push_item_to_list(astra_get_root_list(), item);
}