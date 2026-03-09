#include "ui_render.h"
#include "ui_types.h"
#include "u8g2.h"
#include "board.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

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

/* ================== 向后兼容的渲染函数（旧代码使用） ================== */

static int prev_utf8_start_local(const char *s, int idx) {
  while (idx > 0) {
    idx--;
    if (((unsigned char)s[idx] & 0xC0) != 0x80)
      break;
  }
  return idx;
}

void ui_render_main(int message_count, int unread_count) {
  board_display_begin();
  
  ui_render_status_bar(NULL);
  
  // 获取当前时间
  time_t now;
  time(&now);
  struct tm *t = localtime(&now);
  
  // 使用大字体显示时钟
  if (t) {
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", t->tm_hour, t->tm_min);
    
    // 使用更大的字体绘制时间
    ui_set_font(u8g2_font_logisoso24_tn);
    int time_width = board_display_text_width(time_str);
    int time_x = (128 - time_width) / 2;
    ui_draw_text(time_x, 43, time_str);
    
    // 显示日期 (在时间下方)
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    char date_str[32];
    snprintf(
        date_str, sizeof(date_str), "%d 月%d 日 周%s", t->tm_mon + 1, t->tm_mday,
        (const char *[]){"日", "一", "二", "三", "四", "五", "六"}[t->tm_wday]);
    int date_width = board_display_text_width(date_str);
    ui_draw_text((128 - date_width) / 2, 55, date_str);
  } else {
    // 无法获取时间时显示欢迎语
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    ui_draw_text_centered(0, 35, 128, "BIPI PAGER");
  }
  
  board_display_end();
}

void ui_render_message_read(const ui_message_t *msg, int current_idx,
                            int total_count, int vertical_offset) {
  if (!msg)
    return;
  
  board_display_begin();
  
  char page_str[16];
  snprintf(page_str, sizeof(page_str), "[%d/%d]", current_idx + 1, total_count);
  ui_render_status_bar(page_str);
  
  // 显示发送者，使用用户图标
  ui_set_font(u8g2_font_open_iconic_human_1x_t);
  ui_draw_glyph(0, 25, 0x0040); // 用户图标
  ui_set_font(u8g2_font_wqy12_t_gb2312a);
  
  char header[64];
  snprintf(header, sizeof(header), " %s", msg->sender);
  ui_draw_text(12, 25, header);
  
  // 显示消息内容：按像素宽度换行，并支持垂直偏移以实现滚动
  const int left = 2;
  const int right = 4;
  const int area_width = 128 - left - right;
  const int line_height = 12;
  const int y_start = 38;
  
  const char *p = msg->text;
  int y = y_start - vertical_offset;
  char line_buf[128];
  
  while (*p) {
    // 构建一行，逐字符追加直到超出像素宽度
    int pos = 0;
    int i = 0;
    while (p[i] != '\0') {
      // 找到下一个 UTF-8 字符长度
      unsigned char c = (unsigned char)p[i];
      int char_len = 1;
      if (c < 0x80)
        char_len = 1;
      else if ((c & 0xE0) == 0xC0)
        char_len = 2;
      else if ((c & 0xF0) == 0xE0)
        char_len = 3;
      else if ((c & 0xF8) == 0xF0)
        char_len = 4;
      
      if (pos + char_len >= (int)sizeof(line_buf) - 1)
        break;
      memcpy(&line_buf[pos], &p[i], char_len);
      pos += char_len;
      line_buf[pos] = '\0';
      
      // 测试当前缓冲区宽度
      int w = board_display_text_width(line_buf);
      if (w > area_width) {
        // 回退到上一个 UTF-8 起点
        pos = prev_utf8_start_local(line_buf, pos);
        line_buf[pos] = '\0';
        break;
      }
      
      i += char_len;
    }
    
    if (pos == 0) {
      // 处理极端情况：单个字符宽度超过区域宽度，强制显示一个字符
      int clen = prev_utf8_start_local(p, 1);
      if (clen <= 0)
        clen = 1;
      memcpy(line_buf, p, clen);
      line_buf[clen] = '\0';
      i = clen;
    }
    
    // 只绘制可见区域内的行
    if (y + line_height > 12 && y < 64) {
      ui_draw_text(left, y, line_buf);
    }
    
    y += line_height;
    p += i;
  }
  
  // 如果消息未读，显示未读指示器
  if (!msg->is_read) {
    ui_set_font(u8g2_font_open_iconic_check_1x_t);
    ui_draw_glyph(115, 60, 0x005B); // 勾选图标表示未读
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
  }
  
  board_display_end();
}

void ui_render_standby(void) {
  board_display_begin();
  
  uint32_t now = board_time_ms();
  
  // 周期（毫秒）—— 控制整体速度
  const uint32_t period_ms = 12000; // 12 秒一个完整图案
  
  // 屏幕中心
  const int cx = 64;
  const int cy = 32;
  
  // 振幅（椭圆范围）
  const float a = 55.0f; // X 方向最大偏移
  const float b = 28.0f; // Y 方向最大偏移
  
  // Lissajous 频率比（建议用小整数比，如 2:3, 3:4, 5:4 等）
  const float fx = 3.0f; // X 方向频率
  const float fy = 2.0f; // Y 方向频率
  
  // 相位偏移（弧度），可制造旋转感
  const float px = 0.0f;
  const float py = M_PI / 2.0f; // 90 度相位差 → 更立体
  
  // 时间归一化为 [0, 2π)
  float t = 2.0f * M_PI * ((now % period_ms) / (float)period_ms);
  
  // Lissajous 轨迹
  int scan_x = (int)(cx + a * sinf(fx * t + px));
  int scan_y = (int)(cy + b * sinf(fy * t + py));
  
  // 绘制十字扫描线
  ui_set_draw_color(1);
  ui_draw_rect(0, scan_y, 128, 1, true);   // 水平
  ui_draw_rect(scan_x, 0, 1, 64, true);    // 垂直
  
  // 空心锁定框（7x7）
  const int sq = 7;
  ui_draw_rect(scan_x - sq/2, scan_y - sq/2, sq, sq, false);
  
  // Logo（无抖动，默认字体）
  const char *logo = "BIPUPU";
  int logo_w = board_display_text_width(logo);
  int base_x = (128 - logo_w) / 2;
  ui_draw_text(base_x, 36, logo);
  
  board_display_end();
}

void ui_render_toast_overlay(const char *msg) {
    ui_render_toast(msg);  // 使用新的统一接口
}

void ui_render_logo(void) {
    board_display_begin();
    
    // 使用大号字体显示品牌 LOGO
    ui_set_font(u8g2_font_logisoso24_tn);
    const char *logo = "BIPUPU";
    int logo_w = board_display_text_width(logo);
    ui_draw_text((128 - logo_w) / 2, 36, logo);
    
    board_display_end();
}
