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
    
    ui_list_config_t config = {
        .items = list_items,
        .item_count = count,
        .selected_index = s_ctx.selected_index,
        .scroll_offset = s_ctx.scroll_offset,
        .items_per_page = 4,
        .title = "消息列表",
    };
    
    ui_render_list(&config);
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
                    // NVS 保存由 ui_flush_pending_saves 处理
                }
                
                // TODO: 导航到消息阅读页面
                // ui_navigate_to_page(ui_get_message_page(), NULL);
                ui_change_page(UI_STATE_MESSAGE_READ);
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
