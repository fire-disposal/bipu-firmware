#include "ui.h"
#include "ui_state.h"

// UI 总入口，状态机与页面切换
static ui_page_t current_page = UI_PAGE_IDLE;

void ui_init(void) {
    current_page = UI_PAGE_IDLE;
    // 其他初始化
}

void ui_show_message(const char* sender, const char* text) {
    current_page = UI_PAGE_MESSAGE;
    // 保存消息内容，触发页面刷新
}

void ui_tick(void) {
    // 根据 current_page 调用对应 draw
    switch (current_page) {
        case UI_PAGE_IDLE:
            // ui_draw_idle();
            break;
        case UI_PAGE_MESSAGE:
            // ui_draw_message();
            break;
        case UI_PAGE_LIST:
            // ui_draw_list();
            break;
        case UI_PAGE_SETTINGS:
            // ui_draw_settings();
            break;
    }
}

void ui_on_key(int key) {
    // 分发按键到当前页面
    // 例如：ui_message_on_key(key);
}