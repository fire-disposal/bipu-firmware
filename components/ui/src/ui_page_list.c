#include "ui_page.h"
#include "ui.h"
#include "ui_types.h"
#include "board.h"
#include "esp_log.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char* TAG = "PAGE_LIST";

// 每页显示项数（根据屏幕高度与行高调整）
#define ITEMS_PER_PAGE 3
#define LINE_HEIGHT 14
#define STATUS_BAR_Y 10

static void on_enter(void) {
    ESP_LOGD(TAG, "Entering Message List Page");
}

static void render_list_page(int selected_idx) {
    int total = ui_get_message_count();
    if (total == 0) {
        ui_change_page(UI_STATE_MAIN);
        return;
    }

    // 保证 selected_idx 在合法范围
    if (selected_idx < 0) selected_idx = 0;
    if (selected_idx >= total) selected_idx = total - 1;

    int page = selected_idx / ITEMS_PER_PAGE;
    int total_pages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int start = page * ITEMS_PER_PAGE;

    board_display_begin();

    // 顶部信息栏中部显示页码
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "[%d/%d]", page + 1, total_pages);
    board_display_rect(0, 12, 128, 1, true);
    // 在状态栏中心区域水平居中显示页码，使用安全居中绘制
    const int left_margin = 20;
    const int right_margin = 28;
    const int area_x = left_margin;
    const int area_w = 128 - left_margin - right_margin;
    ui_draw_text_centered(area_x, STATUS_BAR_Y, area_w, page_str);

    // 列表条目区
    int y = STATUS_BAR_Y + 15;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int idx = start + i;
        if (idx >= total) break;
        ui_message_t* msg = ui_get_message_at(idx);
        if (!msg) continue;

        // 时间格式化
        time_t ts = (time_t)msg->timestamp;
        struct tm *tmv = localtime(&ts);
        char timestr[16] = "";
        if (tmv) strftime(timestr, sizeof(timestr), "%H:%M", tmv);

        // 选中项前显示箭头
        if (idx == selected_idx) {
            board_display_text(0, y, ">");
        }

        char line[128];
        // 将 sender 与时间拼接，再用安全裁剪绘制，避免字节截断
        const char *sender = (msg->sender[0]) ? msg->sender : "";
        snprintf(line, sizeof(line), "%s  %s", sender, timestr);
        ui_draw_text_clipped(12, y, 128 - 12 - 4, line);
        y += LINE_HEIGHT;
    }

    board_display_end();
}

static void tick(void) {
    int idx = ui_get_current_message_idx();
    render_list_page(idx);
}

static void on_key(board_key_t key) {
    int total = ui_get_message_count();
    if (total == 0) {
        ui_change_page(UI_STATE_MAIN);
        return;
    }

    int idx = ui_get_current_message_idx();

    if (key == BOARD_KEY_BACK) {
        ui_change_page(UI_STATE_MAIN);
        return;
    }

    if (key == BOARD_KEY_DOWN) {
        if (idx < total - 1) idx++; else idx = 0;
        ui_set_current_message_idx(idx);
        return;
    }

    if (key == BOARD_KEY_UP) {
        if (idx > 0) idx--; else idx = total - 1;
        ui_set_current_message_idx(idx);
        return;
    }

    if (key == BOARD_KEY_ENTER) {
        // 进入消息详情页
        ui_change_page(UI_STATE_MESSAGE_READ);
        return;
    }
}

const ui_page_t page_list = {
    .on_enter = on_enter,
    .tick = tick,
    .on_key = on_key,
};
