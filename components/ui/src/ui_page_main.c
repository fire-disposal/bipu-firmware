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
    if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN) {
        if (ui_get_message_count() > 0) {
            ui_change_page(UI_STATE_MESSAGE_READ);
        }
    }
}

const ui_page_t page_main = {
    .on_enter = on_enter,
    .tick = tick,
    .on_key = on_key
};
