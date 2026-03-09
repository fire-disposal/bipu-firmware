#include "ui_render.h"
#include "u8g2.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "ui_render";

/* ================== 内部辅助函数 ================== */

static int prev_utf8_start(const char *s, int idx) {
    while (idx > 0) {
        idx--;
        if (((unsigned char)s[idx] & 0xC0) != 0x80)
            break;
    }
    return idx;
}

/* ================== 基础绘制原语实现 ================== */

void ui_draw_text(int x, int y, const char* text)
{
    if (text == NULL) return;
    board_display_text(x, y, text);
}

void ui_draw_text_centered(int x, int y, int width, const char* text)
{
    if (text == NULL) return;
    int text_width = board_display_text_width(text);
    int text_x = x + (width - text_width) / 2;
    board_display_text(text_x, y, text);
}

void ui_draw_rect(int x, int y, int w, int h, bool fill)
{
    if (w <= 0 || h <= 0) return;
    board_display_rect(x, y, w, h, fill);
}

void ui_draw_glyph(int x, int y, uint16_t encoding)
{
    board_display_glyph(x, y, encoding);
}

void ui_draw_line(int x1, int y1, int x2, int y2)
{
    board_display_set_draw_color(1);
    
    // 使用 u8g2 画线（需要通过 board 层访问）
    // 暂时用矩形模拟
    if (x1 == x2) {
        // 垂直线
        int h = y2 - y1;
        if (h < 0) h = -h;
        board_display_rect(x1, y1 < y2 ? y1 : y2, 1, h, true);
    } else if (y1 == y2) {
        // 水平线
        int w = x2 - x1;
        if (w < 0) w = -w;
        board_display_rect(x1 < x2 ? x1 : x2, y1, w, 1, true);
    }
}

void ui_set_font(const void* font)
{
    board_display_set_font(font);
}

void ui_set_draw_color(uint8_t color)
{
    board_display_set_draw_color(color);
}

void ui_set_font_mode(uint8_t mode)
{
    board_display_set_font_mode(mode);
}

/* ================== 高级组件实现 ================== */

void ui_render_status_bar(const char* center_text)
{
    board_display_begin();
    
    // 顶部分隔线
    ui_draw_rect(0, 12, 128, 1, true);
    
    // 中间文字（如果有）
    if (center_text != NULL && center_text[0] != '\0') {
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        ui_draw_text_centered(0, 10, 128, center_text);
    }
    
    // 状态栏固定在顶部，实际内容从 y=14 开始
}

void ui_render_list(ui_list_config_t* config)
{
    if (config == NULL) return;
    
    board_display_begin();
    
    // 标题（如果有）
    if (config->title != NULL) {
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        ui_draw_rect(0, 12, 128, 1, true);
        ui_draw_text_centered(0, 10, 128, config->title);
    }
    
    // 计算显示参数
    const int content_start_y = config->title ? 16 : 0;
    const int line_height = 14;
    const int items_per_page = config->items_per_page > 0 ? 
                               config->items_per_page : 4;
    const int total_height = items_per_page * line_height;
    
    // 自动滚动逻辑
    int scroll_offset = config->scroll_offset;
    if (scroll_offset == 0 && config->item_count > items_per_page) {
        // 确保选中项可见
        if (config->selected_index >= scroll_offset + items_per_page) {
            scroll_offset = config->selected_index - items_per_page + 1;
        } else if (config->selected_index < scroll_offset) {
            scroll_offset = config->selected_index;
        }
    }
    
    // 渲染可见项
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    for (int i = 0; i < items_per_page; i++) {
        int item_index = scroll_offset + i;
        if (item_index >= config->item_count) break;
        
        int y = content_start_y + i * line_height + 12;
        const char* item_text = config->items[item_index];
        
        // 选中项高亮
        if (item_index == config->selected_index) {
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

void ui_render_dialog(const char* title, const char* message, 
                      ui_dialog_button_t buttons[])
{
    if (title == NULL || message == NULL) return;
    
    board_display_begin();
    
    // 对话框尺寸
    const int box_width = 120;
    const int box_height = 50;
    const int box_x = (128 - box_width) / 2;
    const int box_y = (64 - box_height) / 2;
    
    // 1. 背景（黑色填充）
    ui_set_draw_color(0);
    ui_draw_rect(box_x, box_y, box_width, box_height, true);
    
    // 2. 边框（白色）
    ui_set_draw_color(1);
    ui_draw_rect(box_x, box_y, box_width, box_height, false);
    
    // 3. 标题
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    ui_draw_text_centered(box_x, box_y + 10, box_width, title);
    
    // 4. 分隔线
    ui_draw_line(box_x + 4, box_y + 14, box_x + box_width - 4, box_y + 14);
    
    // 5. 消息内容
    ui_draw_text_centered(box_x, box_y + 28, box_width, message);
    
    // 6. 按钮（如果有）
    if (buttons != NULL && buttons[0].text != NULL) {
        int button_y = box_y + 42;
        int num_buttons = 0;
        while (buttons[num_buttons].text != NULL && num_buttons < 3) {
            num_buttons++;
        }
        
        int button_width = (box_width - 10) / num_buttons - 4;
        int button_x = box_x + 5;
        
        for (int i = 0; i < num_buttons; i++) {
            if (buttons[i].is_default) {
                ui_set_draw_color(1);
                ui_draw_rect(button_x, button_y, button_width, 12, true);
                ui_set_draw_color(0);
            } else {
                ui_set_draw_color(1);
                ui_draw_rect(button_x, button_y, button_width, 12, false);
            }
            
            ui_draw_text_centered(button_x, button_y + 10, button_width, buttons[i].text);
            button_x += button_width + 4;
        }
    }
    
    board_display_end();
}

void ui_render_simple_dialog(const char* title, const char* message,
                             const char* button1, const char* button2)
{
    ui_dialog_button_t buttons[3] = {0};
    
    if (button1 != NULL) {
        buttons[0] = (ui_dialog_button_t){.text = button1, .is_default = false};
    }
    if (button2 != NULL) {
        buttons[1] = (ui_dialog_button_t){.text = button2, .is_cancel = true};
    }
    
    ui_render_dialog(title, message, buttons);
}

void ui_render_toast(const char* message)
{
    if (message == NULL || message[0] == '\0') return;
    
    board_display_begin();
    
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    int text_width = board_display_text_width(message);
    const int padding = 6;
    const int line_height = 14;
    
    int box_width = text_width + padding * 2;
    if (box_width < 60) box_width = 60;
    
    int box_height = line_height + padding * 2;
    int box_x = (128 - box_width) / 2;
    int box_y = (64 - box_height) / 2;
    
    // 背景（黑色）
    ui_set_draw_color(0);
    ui_draw_rect(box_x, box_y, box_width, box_height, true);
    
    // 边框（白色双线）
    ui_set_draw_color(1);
    ui_draw_rect(box_x, box_y, box_width, box_height, false);
    ui_draw_rect(box_x + 1, box_y + 1, box_width - 2, box_height - 2, false);
    
    // 文字（透明模式）
    ui_set_font_mode(1);
    ui_draw_text_centered(box_x, box_y + padding + line_height - 2, box_width, message);
    
    // 恢复默认
    ui_set_draw_color(1);
    ui_set_font_mode(0);
    
    board_display_end();
}

void ui_render_progress_bar(int x, int y, int width, int height, uint8_t percent)
{
    if (percent > 100) percent = 100;
    
    // 边框
    ui_set_draw_color(1);
    ui_draw_rect(x, y, width, height, false);
    
    // 进度填充
    if (percent > 0) {
        int fill_width = (width - 2) * percent / 100;
        ui_draw_rect(x + 1, y + 1, fill_width, height - 2, true);
    }
}

void ui_render_toggle(int x, int y, const char* label, bool is_on)
{
    // 标签文字
    if (label != NULL) {
        ui_set_font(u8g2_font_wqy12_t_gb2312a);
        ui_draw_text(x, y, label);
        x += board_display_text_width(label) + 8;
    }
    
    // 开关框
    const int toggle_width = 30;
    const int toggle_height = 14;
    
    ui_set_draw_color(1);
    ui_draw_rect(x, y - 10, toggle_width, toggle_height, false);
    
    // 开关滑块
    if (is_on) {
        ui_draw_rect(x + toggle_width - 12, y - 8, 10, 8, true);
    } else {
        ui_draw_rect(x + 2, y - 8, 10, 8, true);
    }
}

/* ================== 布局辅助实现 ================== */

int ui_text_width(const char* text)
{
    if (text == NULL) return 0;
    return board_display_text_width(text);
}

int ui_text_height(void)
{
    // 默认 12px 字体高度
    return 14;  // 包含行间距
}

int ui_center_x(int width, const char* text)
{
    if (text == NULL) return width / 2;
    int text_width = board_display_text_width(text);
    return (width - text_width) / 2;
}
