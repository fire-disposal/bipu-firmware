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
#define ITEMS_PER_PAGE 3
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
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);

    // 顶部标题栏
    board_display_rect(0, 12, 128, 1, true);
    
    // 左侧标题
    board_display_text(4, STATUS_BAR_Y, "收件箱");
    
    // 右侧页码
    char page_str[24];
    snprintf(page_str, sizeof(page_str), "%d/%d", page + 1, total_pages);
    int pw = board_display_text_width(page_str);
    board_display_text(124 - pw, STATUS_BAR_Y, page_str);

    // 列表条目区
    int y = CONTENT_START_Y;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int idx = start + i;
        if (idx >= total) break;
        ui_message_t* msg = ui_get_message_at(idx);
        if (!msg) continue;

        bool is_selected = (idx == selected_idx);
        
        // 选中项：白色填充背景 + 黑色文字（真正反色）
        if (is_selected) {
            board_display_set_draw_color(1);
            board_display_rect(0, y - LINE_HEIGHT + 2, 128, LINE_HEIGHT, true);
            board_display_set_font_mode(1);  // 透明模式
            board_display_set_draw_color(0); // 黑色绘制文字
        }

        // 时间格式化
        time_t ts = (time_t)msg->timestamp;
        struct tm *tmv = localtime(&ts);
        char timestr[16] = "";
        if (tmv) strftime(timestr, sizeof(timestr), "%H:%M", tmv);

        // 选中标记或删除模式标记
        int text_x = 2;
        if (is_selected) {
            if (s_delete_mode) {
                // 删除模式：显示闪烁的 × 符号
                uint32_t elapsed = board_time_ms() - s_delete_anim_start;
                if ((elapsed / 300) % 2 == 0) {
                    board_display_text(text_x, y, "×");
                }
            } else {
                board_display_text(text_x, y, "›");
            }
        }
        text_x += 10;
        
        // 未读标记
        if (!msg->is_read) {
            board_display_text(text_x, y, "•");
        }
        text_x += 10;

        // 发送者名称 (限制宽度)
        const char *sender = (msg->sender[0]) ? msg->sender : "未知";
        ui_draw_text_clipped(text_x, y, 70, sender);
        
        // 时间显示在右侧
        int time_w = board_display_text_width(timestr);
        board_display_text(124 - time_w, y, timestr);
        
        // 恢复正常绘制模式
        if (is_selected) {
            board_display_set_draw_color(1); // 恢复白色
            board_display_set_font_mode(0);  // 恢复实心模式
        }
        
        y += LINE_HEIGHT;
    }
    
    // 底部操作提示（用小字体）
    board_display_rect(0, 52, 128, 1, true);
    board_display_set_font(u8g2_font_5x8_tr);
    if (s_delete_mode) {
        ui_draw_text_centered(0, 63, 128, "OK delete  BK cancel");
    } else {
        ui_draw_text_centered(0, 63, 128, "OK open  UP/DN sel  BK back");
    }
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);

    board_display_end();
}

static void tick(void) {
    // 长按检测
    if (s_back_press_start > 0 && !s_delete_mode) {
        uint32_t now = board_time_ms();
        if (now - s_back_press_start >= LONG_PRESS_MS) {
            s_delete_mode = true;
            s_delete_anim_start = now;
            s_back_press_start = 0;
            ESP_LOGD(TAG, "Long press detected - entering delete mode");
        }
    }
    
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
    .tick = tick,
    .on_key = on_key,
};
