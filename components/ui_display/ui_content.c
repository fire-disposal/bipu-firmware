#include "ui_content.h"
#include "astra_ui_core.h"
#include "astra_ui_item.h"

// UI 内容初始化：创建并注册所有 UI 项
void ui_content_init(void)
{
    // 正确做法：先获取根节点，依次添加子项，最后初始化 AstraUI
    static bool switch_val = false;
    static int16_t slider_val = 5;

    astra_list_item_t* root = astra_get_root_list();

    // 创建并添加各类UI项
    astra_list_item_t* item_switch = astra_new_switch_item("开关项", &switch_val, NULL, NULL, default_icon);
    astra_push_item_to_list(root, item_switch);

    astra_list_item_t* item_button = astra_new_button_item("按钮项", NULL, default_icon);
    astra_push_item_to_list(root, item_button);

    astra_list_item_t* item_slider = astra_new_slider_item("滑块项", &slider_val, 1, 0, 10, NULL, NULL, default_icon);
    astra_push_item_to_list(root, item_slider);

    astra_list_item_t* item_user = astra_new_user_item("自定义项", NULL, NULL, NULL, default_icon);
    astra_push_item_to_list(root, item_user);

    // 必须先添加完所有子项再初始化
    astra_init_core();
}