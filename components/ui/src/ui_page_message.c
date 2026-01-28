#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "ui_types.h"
#include "esp_log.h"

static void on_enter(void) {
    ESP_LOGD("PAGE_MSG", "Entering Message Page");
}

static void tick(void) {
    int count = ui_get_message_count();
    if (count > 0) {
        int idx = ui_get_current_message_idx();
        ui_message_t* msg = ui_get_message_at(idx);
        if (msg) {
            if (!msg->is_read) {
                msg->is_read = true;
            }
            ui_render_message_read(msg, idx, count);
        }
    } else {
        ui_change_page(UI_STATE_MAIN);
    }
}

static void on_key(board_key_t key) {
    int count = ui_get_message_count();
    int idx = ui_get_current_message_idx();

    if (key == BOARD_KEY_BACK) {
        ui_change_page(UI_STATE_MAIN);
    } else if (key == BOARD_KEY_DOWN) {
        if (idx < count - 1) {
            ui_set_current_message_idx(idx + 1);
        } else {
            ui_set_current_message_idx(0);
        }
    } else if (key == BOARD_KEY_UP) {
        if (idx > 0) {
            ui_set_current_message_idx(idx - 1);
        } else {
            ui_set_current_message_idx(count - 1);
        }
    }
}

const ui_page_t page_message = {
    .on_enter = on_enter,
    .tick = tick,
    .on_key = on_key
};
