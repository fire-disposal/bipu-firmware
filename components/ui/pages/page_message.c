#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "u8g2.h"
#include "esp_log.h"

static const char* TAG = "page_message";

/* ================== 页面上下文 ================== */

typedef struct {
    int vertical_offset;
    int max_offset;
    bool is_read;
} message_page_context_t;

static message_page_context_t s_ctx = {0};

/* ================== 页面生命周期回调 ================== */

static void page_message_on_enter(ui_page_base_t* page, void* params)
{
    (void)params;
    ESP_LOGD(TAG, "Entering Message Read Page");
    
    ui_message_t* msg = ui_get_message_at(ui_get_current_message_idx());
    s_ctx.vertical_offset = 0;
    s_ctx.is_read = msg ? msg->is_read : true;
    
    // 计算最大滚动偏移
    if (msg) {
        int text_len = strlen(msg->text);
        int chars_per_line = 20;  // 估算
        int lines = (text_len + chars_per_line - 1) / chars_per_line;
        s_ctx.max_offset = (lines > 4) ? (lines - 4) * 12 : 0;
    }
    
    page_request_render(page);
}

static void page_message_on_exit(ui_page_base_t* page)
{
    (void)page;
    ESP_LOGD(TAG, "Exiting Message Read Page");
}

static void page_message_render(ui_page_base_t* page)
{
    (void)page;
    
    int idx = ui_get_current_message_idx();
    ui_message_t* msg = ui_get_message_at(idx);
    
    if (!msg) return;
    
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "[%d/%d]", idx + 1, ui_get_message_count());
    
    board_display_begin();
    ui_render_status_bar(page_str);
    
    // 发送者
    ui_set_font(u8g2_font_open_iconic_human_1x_t);
    ui_draw_glyph(0, 25, 0x0040);
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    char header[64];
    snprintf(header, sizeof(header), " %s", msg->sender);
    ui_draw_text(12, 25, header);
    
    // 消息内容（带滚动）
    const int left = 2;
    const int right = 4;
    const int area_width = 128 - left - right;
    const int line_height = 12;
    const int y_start = 38;
    
    const char *p = msg->text;
    int y = y_start - s_ctx.vertical_offset;
    char line_buf[128];
    
    while (*p) {
        int pos = 0;
        int i = 0;
        while (p[i] != '\0') {
            unsigned char c = (unsigned char)p[i];
            int char_len = 1;
            if (c >= 0xC0) char_len = 2;
            else if (c >= 0xE0) char_len = 3;
            
            if (pos + char_len >= (int)sizeof(line_buf) - 1) break;
            
            memcpy(&line_buf[pos], &p[i], char_len);
            pos += char_len;
            line_buf[pos] = '\0';
            
            int w = board_display_text_width(line_buf);
            if (w > area_width) {
                // 回退
                while (pos > 0) {
                    pos--;
                    if ((line_buf[pos] & 0xC0) != 0x80) {
                        pos++;
                        break;
                    }
                }
                line_buf[pos] = '\0';
                break;
            }
            
            i += char_len;
        }
        
        if (pos == 0) break;
        
        if (y + line_height > 12 && y < 64) {
            ui_draw_text(left, y, line_buf);
        }
        
        y += line_height;
        p += i;
    }
    
    // 未读标记
    if (!s_ctx.is_read) {
        ui_set_font(u8g2_font_open_iconic_check_1x_t);
        ui_draw_glyph(115, 60, 0x005B);
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
    }
    
    board_display_end();
}

static void page_message_on_key(ui_page_base_t* page, board_key_t key)
{
    switch (key) {
        case BOARD_KEY_BACK:
            ui_go_back_page();
            break;
            
        case BOARD_KEY_DOWN:
        case BOARD_KEY_DOWN_REPEAT:
            if (s_ctx.vertical_offset < s_ctx.max_offset) {
                s_ctx.vertical_offset += 12;
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_UP:
        case BOARD_KEY_UP_REPEAT:
            if (s_ctx.vertical_offset > 0) {
                s_ctx.vertical_offset -= 12;
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_ENTER:
            // 删除消息
            ui_delete_current_message();
            ui_go_back_page();
            break;
            
        default:
            break;
    }
}

/* ================== 页面对象定义 ================== */

static ui_page_base_t s_message_page = {
    .name = "MessageRead",
    .on_enter = page_message_on_enter,
    .on_exit = page_message_on_exit,
    .update = NULL,  // 不需要周期性更新
    .render = page_message_render,
    .on_key = page_message_on_key,
    .update_interval = 0,
    .context = &s_ctx,
};

/* ================== 公开接口 ================== */

ui_page_base_t* ui_get_message_page(void)
{
    return &s_message_page;
}
