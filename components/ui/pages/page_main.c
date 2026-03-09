#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "ui_state_machine.h"
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

/* ================== 页面生命周期回调 ================== */

static void on_enter(ui_page_base_t* page, void* params)
{
    (void)params;
    ESP_LOGD(TAG, "Entering Main Page");
    
    // 重置上下文
    s_ctx.total_msgs = 0;
    s_ctx.unread_msgs = 0;
    s_ctx.last_update_time = 0;
    
    page_request_render(page);
}

static void on_exit(ui_page_base_t* page)
{
    (void)page;
    ESP_LOGD(TAG, "Exiting Main Page");
}

static void update(ui_page_base_t* page, uint32_t delta_ms)
{
    (void)page;
    (void)delta_ms;
    
    // 每秒更新一次消息计数
    uint32_t now = board_time_ms();
    if (now - s_ctx.last_update_time >= 1000) {
        s_ctx.last_update_time = now;
        s_ctx.total_msgs = ui_get_message_count();
        s_ctx.unread_msgs = ui_get_unread_count();
    }
}

static void render(ui_page_base_t* page)
{
    (void)page;
    
    board_display_begin();
    
    // 1. 渲染状态栏
    ui_render_status_bar(NULL);
    
    // 2. 获取当前时间
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    
    // 3. 渲染时钟
    if (t) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", t->tm_hour, t->tm_min);
        
        // 大字体时间
        ui_set_font(u8g2_font_logisoso24_tn);
        ui_draw_text_centered(0, 43, 128, time_str);
        
        // 日期
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        char date_str[32];
        snprintf(date_str, sizeof(date_str), "%d月%d日 周%s", 
                 t->tm_mon + 1, t->tm_mday,
                 (const char*[]){"日", "一", "二", "三", "四", "五", "六"}[t->tm_wday]);
        ui_draw_text_centered(0, 55, 128, date_str);
    } else {
        // 无法获取时间
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        ui_draw_text_centered(0, 35, 128, "BIPI PAGER");
    }
    
    // 4. 消息提示（如果有未读）
    if (s_ctx.unread_msgs > 0) {
        ui_set_font(u8g2_font_open_iconic_email_1x_t);
        ui_draw_glyph(115, 10, 0x0041);  // 邮件图标
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        
        char msg_str[16];
        snprintf(msg_str, sizeof(msg_str), "%d", s_ctx.unread_msgs);
        ui_draw_text(105, 10, msg_str);
    }
    
    board_display_end();
}

static void on_key(ui_page_base_t* page, board_key_t key)
{
    switch (key) {
        case BOARD_KEY_ENTER:
        case BOARD_KEY_DOWN:
            // 进入消息列表
            if (ui_get_message_count() > 0) {
                int cnt = ui_get_message_count();
                ui_set_current_message_idx(cnt - 1);
                ui_navigate_to(page, ui_get_list_page(), NULL);
            } else {
                ui_show_toast("暂无消息", 1500);
            }
            break;
            
        case BOARD_KEY_UP:
            // 进入设置页面
            ui_navigate_to(page, ui_get_settings_page(), NULL);
            break;
            
        case BOARD_KEY_BACK_LONG:
            // 切换手电筒
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
    .on_enter = on_enter,
    .on_exit = on_exit,
    .update = update,
    .render = render,
    .on_key = on_key,
    .update_interval = 1000,  // 1 秒更新一次
    .context = &s_ctx,
};

/* ================== 公开接口 ================== */

ui_page_base_t* ui_get_main_page(void)
{
    return &s_main_page;
}
