#include "ui_page.h"
#include "ui_render.h"
#include "ui.h"
#include "ui_types.h"
#include "esp_log.h"

static int s_vertical_offset = 0;

static void on_enter(void) {
    ESP_LOGD("PAGE_MSG", "Entering Message Page");
}

static void tick(void) {
    int count = ui_get_message_count();
    if (count <= 0) {
        ui_change_page(UI_STATE_MAIN);
        return;
    }

    int idx = ui_get_current_message_idx();
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    ui_set_current_message_idx(idx);

    ui_message_t* msg = ui_get_message_at(idx);
    if (msg) {
        if (!msg->is_read) {
            msg->is_read = true;
        }
        ui_render_message_read(msg, idx, count, s_vertical_offset);
    }
}

static void on_key(board_key_t key) {
    int count = ui_get_message_count();
    int idx = ui_get_current_message_idx();
    
    ESP_LOGD("PAGE_MSG", "Message page received key: %d, current idx: %d, total: %d", key, idx, count);

    // 处理短按/长按：短按滚动文本，长按切换消息（由重复事件识别）
    static board_key_t last_key = BOARD_KEY_NONE;
    static uint32_t last_key_time = 0;
    const int line_height = 12;

    uint32_t now = board_time_ms();

    if (key == BOARD_KEY_BACK) {
        ESP_LOGD("PAGE_MSG", "BACK key pressed, returning to main page");
        ui_change_page(UI_STATE_MAIN);
    } else if (key == BOARD_KEY_DOWN) {
        // 判断是否为重复事件（长按）
        if (last_key == key && now - last_key_time >= 200) {
            ESP_LOGD("PAGE_MSG", "Long-press DOWN -> switch to next message");
            if (idx < count - 1) ui_set_current_message_idx(idx + 1);
            else ui_set_current_message_idx(0);
            s_vertical_offset = 0;
        } else {
            ESP_LOGD("PAGE_MSG", "Short-press DOWN -> scroll down content");
            // 计算总文本高度以限制滚动（简单估算：在渲染时会被裁剪）
            // 向下滚动一行
            s_vertical_offset += line_height;
            if (s_vertical_offset < 0) s_vertical_offset = 0;
        }
    } else if (key == BOARD_KEY_UP) {
        if (last_key == key && now - last_key_time >= 200) {
            ESP_LOGD("PAGE_MSG", "Long-press UP -> switch to previous message");
            if (idx > 0) ui_set_current_message_idx(idx - 1);
            else ui_set_current_message_idx(count - 1);
            s_vertical_offset = 0;
        } else {
            ESP_LOGD("PAGE_MSG", "Short-press UP -> scroll up content");
            s_vertical_offset -= line_height;
            if (s_vertical_offset < 0) s_vertical_offset = 0;
        }
    } else {
        ESP_LOGD("PAGE_MSG", "Key %d ignored in message page", key);
    }
    last_key = key;
    last_key_time = now;
}

const ui_page_t page_message = {
    .on_enter = on_enter,
    .tick = tick,
    .on_key = on_key
};
