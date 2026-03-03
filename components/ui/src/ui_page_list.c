#include "ui_page.h"
#include "ui.h"
#include "ui_types.h"
#include "board.h"
#include "u8g2.h"
#include "esp_log.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char* TAG = "PAGE_LIST";

// 每页显示项数（根据屏幕高度与行高调整）
#define ITEMS_PER_PAGE 4
#define LINE_HEIGHT 12
#define STATUS_BAR_Y 10
#define CONTENT_START_Y 24
#define LONG_PRESS_MS 1000  // 长按删除的时间阈值

// 页面状态
static uint32_t s_back_press_start = 0;
static bool s_delete_mode = false;
static uint32_t s_delete_anim_start = 0;  // 删除动画开始时间

static void page_on_enter(void) {
    ESP_LOGD(TAG, "Entering Message List Page");
    s_back_press_start = 0;
    s_delete_mode = false;
    s_delete_anim_start = 0;
}

static void page_on_exit(void) {
    s_delete_mode = false;
    s_back_press_start = 0;
}

// 渲染上下文 (用于 update -> render 传递数据，避免渲染时竞态)
typedef struct {
    int total_pages;
    int current_page;
    struct {
        char sender[32];
        uint32_t timestamp;
        bool is_read;
        bool is_selected;
        bool valid;
    } items[ITEMS_PER_PAGE];
} list_render_ctx_t;

static list_render_ctx_t s_ctx;

static uint32_t update(void) {
    // 1. 长按检测逻辑
    if (s_back_press_start > 0 && !s_delete_mode) {
        uint32_t now = board_time_ms();
        if (now - s_back_press_start >= LONG_PRESS_MS) {
            s_delete_mode = true;
            s_delete_anim_start = now;
            s_back_press_start = 0;
            ESP_LOGD(TAG, "Long press detected - entering delete mode");
        }
        // 按下期间高频检查
        return 100;
    }

    // 2. 准备渲染数据 (在锁保护下执行)
    int total = ui_get_message_count();
    int selected_idx = ui_get_current_message_idx();
    
    // 校验
    if (selected_idx < 0) selected_idx = 0;
    if (selected_idx >= total && total > 0) selected_idx = total - 1;

    int page = selected_idx / ITEMS_PER_PAGE;
    s_ctx.total_pages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    s_ctx.current_page = page + 1;

    int start = page * ITEMS_PER_PAGE;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int idx = start + i;
        if (idx < total) {
            ui_message_t* msg = ui_get_message_at(idx);
            if (msg) {
                strncpy(s_ctx.items[i].sender, msg->sender, sizeof(s_ctx.items[i].sender)-1);
                s_ctx.items[i].sender[31] = '\0';
                s_ctx.items[i].timestamp = msg->timestamp;
                s_ctx.items[i].is_read = msg->is_read;
                s_ctx.items[i].is_selected = (idx == selected_idx);
                s_ctx.items[i].valid = true;
            } else {
                s_ctx.items[i].valid = false;
            }
        } else {
            s_ctx.items[i].valid = false;
        }
    }

    // 普通状态下不需要高频刷新，除非在删除模式动画
    if (s_delete_mode) return 100; // 动画刷新
    return 1000;
}

static void render(void) {
    if (s_ctx.total_pages == 0 && !s_delete_mode) {
        // 如果没有数据且不是删除模式，理论上应该切回主页，但渲染层不应控制逻辑
        return; 
    }

    board_display_begin();
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);

    // 顶部标题栏
    board_display_rect(0, 12, 128, 1, true);
    board_display_text(4, STATUS_BAR_Y, "收件箱");
    
    char page_str[24];
    snprintf(page_str, sizeof(page_str), "%d/%d", s_ctx.current_page, s_ctx.total_pages);
    int pw = board_display_text_width(page_str);
    board_display_text(124 - pw, STATUS_BAR_Y, page_str);

    // 列表条目区
    int y = CONTENT_START_Y;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        if (!s_ctx.items[i].valid) continue;

        bool is_selected = s_ctx.items[i].is_selected;
        
        if (is_selected) {
            board_display_set_draw_color(1);
            board_display_rect(0, y - LINE_HEIGHT + 2, 128, LINE_HEIGHT, true);
            board_display_set_font_mode(1);
            board_display_set_draw_color(0);
        }

        time_t ts = (time_t)s_ctx.items[i].timestamp;
        struct tm *tmv = localtime(&ts);
        char timestr[16] = "";
        if (tmv) strftime(timestr, sizeof(timestr), "%H:%M", tmv);

        int text_x = 2;
        if (is_selected) {
            if (s_delete_mode) {
                uint32_t elapsed = board_time_ms() - s_delete_anim_start;
                if ((elapsed / 300) % 2 == 0) {
                    board_display_text(text_x, y, "×");
                }
            } else {
                board_display_text(text_x, y, "›");
            }
        }
        text_x += 10;
        
        if (!s_ctx.items[i].is_read) {
            board_display_text(text_x, y, "•");
        }
        text_x += 10;

        const char *sender = (s_ctx.items[i].sender[0]) ? s_ctx.items[i].sender : "未知";
        ui_draw_text_clipped(text_x, y, 70, sender);
        
        int time_w = board_display_text_width(timestr);
        board_display_text(124 - time_w, y, timestr);
        
        if (is_selected) {
            board_display_set_draw_color(1);
            board_display_set_font_mode(0);
        }
        
        y += LINE_HEIGHT;
    }
    
    board_display_end();
}

static void on_key(board_key_t key) {
    int total = ui_get_message_count();
    if (total == 0) {
        ui_change_page(UI_STATE_MAIN);
        return;
    }

    int idx = ui_get_current_message_idx();
    
    // 删除确认模式
    if (s_delete_mode) {
        if (key == BOARD_KEY_ENTER) {
            // 确认删除
            ESP_LOGI(TAG, "Deleting message at index %d", idx);
            ui_delete_current_message();
            s_delete_mode = false;
            if (ui_get_message_count() == 0) {
                ui_change_page(UI_STATE_MAIN);
            }
            return;
        } else if (key == BOARD_KEY_BACK) {
            // 取消删除
            s_delete_mode = false;
            return;
        }
        // 删除模式下忽略其他按键
        return;
    }

    switch (key) {
        case BOARD_KEY_BACK:
            // 短按返回主页，长按进入删除模式 (在tick中检测长按)
            if (s_back_press_start == 0) {
                s_back_press_start = board_time_ms();
            } else {
                // 短按释放 - 返回
                ui_change_page(UI_STATE_MAIN);
            }
            return;
            
        case BOARD_KEY_DOWN:
            s_back_press_start = 0;
            if (idx < total - 1) {
                idx++;
            } else {
                idx = 0;  // 循环到开头
            }
            ui_set_current_message_idx(idx);
            return;
            
        case BOARD_KEY_UP:
            s_back_press_start = 0;
            if (idx > 0) {
                idx--;
            } else {
                idx = total - 1;  // 循环到末尾
            }
            ui_set_current_message_idx(idx);
            return;
            
        case BOARD_KEY_ENTER:
            s_back_press_start = 0;
            // 进入消息详情页
            ui_change_page(UI_STATE_MESSAGE_READ);
            return;
            
        default:
            s_back_press_start = 0;
            break;
    }
}

const ui_page_t page_list = {
    .on_enter = page_on_enter,
    .on_exit = page_on_exit,
    .update = update,
    .render = render,
    .on_key = on_key,
};
