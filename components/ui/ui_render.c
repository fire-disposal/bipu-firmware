#include "ui_render.h"
#include "ble_manager.h"
#include "board.h"
#include "u8g2.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_status.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

// 本文件内使用的 UTF-8 回退辅助函数（用于保证不截断字符）
static int prev_utf8_start_local(const char *s, int idx) {
  while (idx > 0) {
    idx--;
    if (((unsigned char)s[idx] & 0xC0) != 0x80)
      break;
  }
  return idx;
}

static void ui_render_status_bar(const char *center_text) {
  ui_status_render(center_text);
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
    board_display_set_font(u8g2_font_logisoso24_tn);
    int time_width = board_display_text_width(time_str);
    int time_x = (128 - time_width) / 2;
    board_display_text(time_x, 43, time_str);

    // 显示日期 (在时间下方)
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    char date_str[32];
    snprintf(
        date_str, sizeof(date_str), "%d月%d日 周%s", t->tm_mon + 1, t->tm_mday,
        (const char *[]){"日", "一", "二", "三", "四", "五", "六"}[t->tm_wday]);
    int date_width = board_display_text_width(date_str);
    board_display_text((128 - date_width) / 2, 55, date_str);
  } else {
    // 无法获取时间时显示欢迎语
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
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
  board_display_set_font(u8g2_font_open_iconic_human_1x_t);
  board_display_glyph(0, 25, ICON_USER_1X); // 用户图标
  board_display_set_font(u8g2_font_wqy12_t_gb2312a);

  char header[64];
  snprintf(header, sizeof(header), " %s", msg->sender);
  board_display_text(12, 25, header);

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
      board_display_text(left, y, line_buf);
    }

    y += line_height;
    p += i;
  }

  // 如果消息未读，显示未读指示器
  if (!msg->is_read) {
    board_display_set_font(u8g2_font_open_iconic_check_1x_t);
    board_display_glyph(115, 60, ICON_CHECK_1X); // 勾选图标表示未读
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
  }

  board_display_end();
}


void ui_render_standby(void) {
  board_display_begin();

  uint32_t now = board_time_ms();

  // 周期（毫秒）—— 控制整体速度
  const uint32_t period_ms = 12000; // 12秒一个完整图案

  // 屏幕中心
  const int cx = 64;
  const int cy = 32;

  // 振幅（椭圆范围）
  const float a = 55.0f; // X方向最大偏移
  const float b = 28.0f; // Y方向最大偏移

  // Lissajous 频率比（建议用小整数比，如 2:3, 3:4, 5:4 等）
  const float fx = 3.0f; // X方向频率
  const float fy = 2.0f; // Y方向频率

  // 相位偏移（弧度），可制造旋转感
  const float px = 0.0f;
  const float py = M_PI / 2.0f; // 90度相位差 → 更立体

  // 时间归一化为 [0, 2π)
  float t = 2.0f * M_PI * ((now % period_ms) / (float)period_ms);

  // Lissajous 轨迹
  int scan_x = (int)(cx + a * sinf(fx * t + px));
  int scan_y = (int)(cy + b * sinf(fy * t + py));

  // 绘制十字扫描线
  board_display_set_draw_color(1);
  board_display_rect(0, scan_y, 128, 1, true);   // 水平
  board_display_rect(scan_x, 0, 1, 64, true);    // 垂直

  // 空心锁定框（7x7）
  const int sq = 7;
  board_display_rect(scan_x - sq/2, scan_y - sq/2, sq, sq, false);

  // Logo（无抖动，默认字体）
  const char *logo = "BIPUPU";
  int logo_w = board_display_text_width(logo);
  int base_x = (128 - logo_w) / 2;
  board_display_text(base_x, 36, logo);

  board_display_end();
}