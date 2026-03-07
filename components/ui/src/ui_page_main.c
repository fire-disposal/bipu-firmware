#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "board.h"
#include "esp_log.h"

static const char* TAG = "PAGE_MAIN";

static void page_on_enter(void) {
    ESP_LOGD(TAG, "Entering Main Page");
}

static void page_on_exit(void) {
}

// 渲染上下文
typedef struct {
    int total_msgs;
    int unread_msgs;
} main_render_ctx_t;

static main_render_ctx_t s_ctx;

static uint32_t update(void) {
    s_ctx.total_msgs = ui_get_message_count();
    s_ctx.unread_msgs = ui_get_unread_count();
    return 1000; // 每秒更新一次时钟
}

static void render(void) {
    ui_render_main(s_ctx.total_msgs, s_ctx.unread_msgs);
}

static void on_key(board_key_t key) {
    ESP_LOGD(TAG, "Main page received key: %d", key);
    
    switch (key) {
        case BOARD_KEY_ENTER:
            // 确认键：进入消息列表（如果有消息）
            if (ui_get_message_count() > 0) {
                int cnt = ui_get_message_count();
                if (cnt > 0) ui_set_current_message_idx(cnt - 1);
                ui_change_page(UI_STATE_MESSAGE_LIST);
            } else {
                ui_show_toast("暂无消息", 1500);
            }
            break;
            
        case BOARD_KEY_DOWN:
            // 下键：进入消息列表
            if (ui_get_message_count() > 0) {
                int cnt = ui_get_message_count();
                if (cnt > 0) ui_set_current_message_idx(cnt - 1);
                ui_change_page(UI_STATE_MESSAGE_LIST);
            } else {
                ui_show_toast("暂无消息", 1500);
            }
            break;
            
        case BOARD_KEY_UP:
            // 上键：进入设置页面
            ESP_LOGD(TAG, "Entering settings");
            ui_change_page(UI_STATE_SETTINGS);
            break;
            
        case BOARD_KEY_BACK_LONG:
            // 长按返回键：切换手电筒
            ESP_LOGD(TAG, "Long press BACK - toggle flashlight");
            ui_toggle_flashlight();
            ui_show_toast(ui_is_flashlight_on() ? "手电筒 已开启" : "手电筒 已关闭", 1500);
            break;
            
        default:
            break;
    }
}

const ui_page_t page_main = {
    .on_enter = page_on_enter,
    .on_exit = page_on_exit,
    .update = update,
    .render = render,
    .on_key = on_key
};
