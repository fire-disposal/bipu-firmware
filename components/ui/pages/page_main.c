#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "u8g2.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char* TAG = "page_main";

/* ================== 页面上下文 ================== */

typedef struct {
    int total_msgs;
    int unread_msgs;
    uint32_t last_update_time;
} main_page_context_t;

static main_page_context_t s_ctx = {0};

/* ================== 渲染状态栏（统一风格） ================== */

static void render_status_bar(void)
{
    // 顶部分隔线
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    ui_draw_rect(0, 12, 128, 1, true);
    
    // 时间显示
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    
    if (t) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", t->tm_hour, t->tm_min);
        int time_width = board_display_text_width(time_str);
        ui_draw_text(126 - time_width, 10, time_str);
    }
}

/* ================== 页面生命周期回调 ================== */

static void page_main_on_enter(ui_page_base_t* page, void* params)
{
    (void)params;
    ESP_LOGD(TAG, "Entering Main Page");
    s_ctx.total_msgs = 0;
    s_ctx.unread_msgs = 0;
    s_ctx.last_update_time = 0;
    page_request_render(page);
}

static void page_main_on_exit(ui_page_base_t* page)
{
    (void)page;
    ESP_LOGD(TAG, "Exiting Main Page");
}

static void page_main_update(ui_page_base_t* page, uint32_t delta_ms)
{
    (void)page;
    (void)delta_ms;
    
    uint32_t now = board_time_ms();
    if (now - s_ctx.last_update_time >= 1000) {
        s_ctx.last_update_time = now;
        s_ctx.total_msgs = ui_get_message_count();
        s_ctx.unread_msgs = ui_get_unread_count();
    }
}

static void page_main_render(ui_page_base_t* page)
{
    (void)page;
    
    board_display_begin();
    
    // 渲染状态栏（顶部）
    render_status_bar();
    
    // 主内容：时间和日期
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    
    if (t) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", t->tm_hour, t->tm_min);
        
        ui_set_font(u8g2_font_logisoso24_tn);
        int time_width = board_display_text_width(time_str);
        ui_draw_text((128 - time_width) / 2, 43, time_str);
        
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        char date_str[32];
        snprintf(date_str, sizeof(date_str), "%d 月%d 日 周%s", 
                 t->tm_mon + 1, t->tm_mday,
                 (const char*[]){"日", "一", "二", "三", "四", "五", "六"}[t->tm_wday]);
        int date_width = board_display_text_width(date_str);
        ui_draw_text((128 - date_width) / 2, 55, date_str);
    } else {
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        ui_draw_text_centered(0, 35, 128, "BIPI PAGER");
    }
    
    // 未读消息提示（状态栏区域）
    if (s_ctx.unread_msgs > 0) {
        ui_set_font(u8g2_font_open_iconic_email_1x_t);
        ui_draw_glyph(115, 10, 0x0041);
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        
        char msg_str[16];
        snprintf(msg_str, sizeof(msg_str), "%d", s_ctx.unread_msgs);
        ui_draw_text(105, 10, msg_str);
    }
    
    board_display_end();
}

static void page_main_on_key(ui_page_base_t* page, board_key_t key)
{
    (void)page;
    
    switch (key) {
        case BOARD_KEY_ENTER:
        case BOARD_KEY_DOWN:
            if (ui_get_message_count() > 0) {
                int cnt = ui_get_message_count();
                ui_set_current_message_idx(cnt - 1);
                ui_navigate_to_page(ui_get_list_page(), NULL);
            } else {
                ui_show_toast("暂无消息", 1500);
            }
            break;
            
        case BOARD_KEY_UP:
            ui_navigate_to_page(ui_get_settings_page(), NULL);
            break;
            
        case BOARD_KEY_BACK_LONG:
            ui_toggle_flashlight();
            ui_show_toast(ui_is_flashlight_on() ? "手电筒 已开启" : "手电筒 已关闭", 1500);
            break;
            
        default:
            break;
    }
}

/* ================== 页面对象定义 ================== */

static ui_page_base_t s_main_page = {
    .name = "Main",
    .on_enter = page_main_on_enter,
    .on_exit = page_main_on_exit,
    .update = page_main_update,
    .render = page_main_render,
    .on_key = page_main_on_key,
    .update_interval = 1000,
    .context = &s_ctx,
};

/* ================== 公开接口 ================== */

ui_page_base_t* ui_get_main_page(void)
{
    return &s_main_page;
}
