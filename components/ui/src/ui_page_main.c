#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "board.h"
#include "esp_log.h"

static const char* TAG = "PAGE_MAIN";

// 长按检测
#define LONG_PRESS_THRESHOLD_MS 800
static uint32_t s_back_press_start = 0;
static bool s_back_long_pressed = false;

static void page_on_enter(void) {
    ESP_LOGD(TAG, "Entering Main Page");
    s_back_press_start = 0;
    s_back_long_pressed = false;
}

static void page_on_exit(void) {
    s_back_press_start = 0;
    s_back_long_pressed = false;
}

static void tick(void) {
    // 检测长按状态 (用于手电筒)
    if (s_back_press_start > 0 && !s_back_long_pressed) {
        uint32_t duration = board_time_ms() - s_back_press_start;
        if (duration >= LONG_PRESS_THRESHOLD_MS) {
            s_back_long_pressed = true;
            ui_toggle_flashlight();
            ESP_LOGD(TAG, "Long press detected - flashlight toggled");
        }
    }
    
    int total = ui_get_message_count();
    int unread = ui_get_unread_count();
    ui_render_main(total, unread);
}

static void on_key(board_key_t key) {
    ESP_LOGD(TAG, "Main page received key: %d", key);
    
    switch (key) {
        case BOARD_KEY_ENTER:
            // 确认键：进入消息列表（如果有消息）
            if (ui_get_message_count() > 0) {
                ESP_LOGD(TAG, "Entering message list");
                // 更友好的默认选中：跳转到最近一条消息
                int cnt = ui_get_message_count();
                if (cnt > 0) ui_set_current_message_idx(cnt - 1);
                ui_change_page(UI_STATE_MESSAGE_LIST);
            }
            break;
            
        case BOARD_KEY_DOWN:
            // 下键：进入消息列表
            if (ui_get_message_count() > 0) {
                int cnt = ui_get_message_count();
                if (cnt > 0) ui_set_current_message_idx(cnt - 1);
                ui_change_page(UI_STATE_MESSAGE_LIST);
            }
            break;
            
        case BOARD_KEY_UP:
            // 上键：进入设置页面
            ESP_LOGD(TAG, "Entering settings");
            ui_change_page(UI_STATE_SETTINGS);
            break;
            
        case BOARD_KEY_BACK:
            // 短按返回键：记录按下时间，长按切换手电筒 (在tick中检测)
            if (s_back_press_start == 0) {
                s_back_press_start = board_time_ms();
                s_back_long_pressed = false;
            }
            break;
            
        default:
            break;
    }
    
    // 非BACK键时重置状态
    if (key != BOARD_KEY_BACK) {
        s_back_press_start = 0;
        s_back_long_pressed = false;
    }
}

const ui_page_t page_main = {
    .on_enter = page_on_enter,
    .on_exit = page_on_exit,
    .tick = tick,
    .on_key = on_key
};
