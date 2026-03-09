#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "u8g2.h"
#include "esp_log.h"

static const char* TAG = "page_list";

/* ================== 页面上下文 ================== */

typedef struct {
    int scroll_offset;
    int selected_index;
    int total_count;
    int unread_count;
} list_page_context_t;

static list_page_context_t s_ctx = {0};

/* ================== 渲染状态栏（统一风格） ================== */

static void render_status_bar(void)
{
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    ui_draw_text_centered(0, 10, 128, "消息列表");
    ui_draw_rect(0, 12, 128, 1, true);
}

/* ================== 页面生命周期回调 ================== */

static void page_list_on_enter(ui_page_base_t* page, void* params)
{
    (void)params;
    ESP_LOGD(TAG, "Entering Message List Page");
    s_ctx.scroll_offset = 0;
    s_ctx.selected_index = ui_get_current_message_idx();
    s_ctx.total_count = ui_get_message_count();
    s_ctx.unread_count = ui_get_unread_count();
    page_request_render(page);
}

static void page_list_on_exit(ui_page_base_t* page)
{
    (void)page;
    ESP_LOGD(TAG, "Exiting Message List Page");
}

static void page_list_update(ui_page_base_t* page, uint32_t delta_ms)
{
    (void)page;
    (void)delta_ms;
    
    // 更新消息计数
    s_ctx.total_count = ui_get_message_count();
    s_ctx.unread_count = ui_get_unread_count();
}

static void page_list_render(ui_page_base_t* page)
{
    (void)page;
    
    board_display_begin();
    
    // 渲染状态栏
    render_status_bar();
    
    // 准备列表数据
    static const char* list_items[20];
    int count = s_ctx.total_count < 20 ? s_ctx.total_count : 20;
    
    for (int i = 0; i < count; i++) {
        ui_message_t* msg = ui_get_message_at(i);
        static char buf[20][64];
        
        if (msg) {
            if (msg->is_read) {
                snprintf(buf[i], sizeof(buf[i]), "%s", msg->sender);
            } else {
                snprintf(buf[i], sizeof(buf[i]), "● %s", msg->sender);  // 未读标记
            }
            list_items[i] = buf[i];
        }
    }
    
    // 内容从 y=16 开始（状态栏下方）
    const int content_start_y = 16;
    const int line_height = 14;
    
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    for (int i = 0; i < 4; i++) {
        int item_index = s_ctx.scroll_offset + i;
        if (item_index >= count) break;
        
        int y = content_start_y + i * line_height + 12;
        const char* item_text = list_items[item_index];
        
        // 选中项高亮
        if (item_index == s_ctx.selected_index) {
            ui_set_draw_color(1);
            ui_draw_rect(0, y - 12, 128, line_height, true);
            ui_set_draw_color(0);
            ui_draw_text(4, y, item_text);
        } else {
            ui_set_draw_color(1);
            ui_draw_text(4, y, item_text);
        }
    }
    
    board_display_end();
}

static void page_list_on_key(ui_page_base_t* page, board_key_t key)
{
    switch (key) {
        case BOARD_KEY_BACK:
            ui_go_back_page();
            break;
            
        case BOARD_KEY_DOWN:
        case BOARD_KEY_DOWN_REPEAT:
            if (s_ctx.selected_index < s_ctx.total_count - 1) {
                s_ctx.selected_index++;
                // 自动滚动
                if (s_ctx.selected_index >= s_ctx.scroll_offset + 4) {
                    s_ctx.scroll_offset = s_ctx.selected_index - 3;
                }
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_UP:
        case BOARD_KEY_UP_REPEAT:
            if (s_ctx.selected_index > 0) {
                s_ctx.selected_index--;
                // 自动滚动
                if (s_ctx.selected_index < s_ctx.scroll_offset) {
                    s_ctx.scroll_offset = s_ctx.selected_index;
                }
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_ENTER:
            if (s_ctx.total_count > 0) {
                ui_set_current_message_idx(s_ctx.selected_index);
                
                // 标记为已读
                ui_message_t* msg = ui_get_message_at(s_ctx.selected_index);
                if (msg && !msg->is_read) {
                    msg->is_read = true;
                }
                
                ui_navigate_to_page(ui_get_message_page(), NULL);
            }
            break;
            
        default:
            break;
    }
}

/* ================== 页面对象定义 ================== */

static ui_page_base_t s_list_page = {
    .name = "MessageList",
    .on_enter = page_list_on_enter,
    .on_exit = page_list_on_exit,
    .update = page_list_update,
    .render = page_list_render,
    .on_key = page_list_on_key,
    .update_interval = 1000,
    .context = &s_ctx,
};

/* ================== 公开接口 ================== */

ui_page_base_t* ui_get_list_page(void)
{
    return &s_list_page;
}
