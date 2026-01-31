#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "esp_log.h"

static void on_enter(void) {
    ESP_LOGD("PAGE_MAIN", "Entering Main Page");
}

static void tick(void) {
    int total = ui_get_message_count();
    int unread = ui_get_unread_count();
    ui_render_main(total, unread);
}

static void on_key(board_key_t key) {
    ESP_LOGD("PAGE_MAIN", "Main page received key: %d", key);
    if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN) {
        ESP_LOGD("PAGE_MAIN", "Enter or Down key pressed, checking messages");
        if (ui_get_message_count() > 0) {
            ESP_LOGD("PAGE_MAIN", "Switching to message list page");
            ui_set_current_message_idx(0);
            ui_change_page(UI_STATE_MESSAGE_LIST);
        } else {
            ESP_LOGD("PAGE_MAIN", "No messages available");
        }
    } else if (key == BOARD_KEY_UP) {
        ESP_LOGD("PAGE_MAIN", "UP key pressed in main page, also switching to messages");
        if (ui_get_message_count() > 0) {
            ESP_LOGD("PAGE_MAIN", "Switching to message list page via UP key");
            ui_set_current_message_idx(0);
            ui_change_page(UI_STATE_MESSAGE_LIST);
        } else {
            ESP_LOGD("PAGE_MAIN", "No messages available for UP key");
        }
    } else {
        ESP_LOGD("PAGE_MAIN", "Key %d ignored in main page", key);
    }
}

const ui_page_t page_main = {
    .on_enter = on_enter,
    .tick = tick,
    .on_key = on_key
};
