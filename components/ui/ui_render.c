#include "ui_render.h"
#include "ble_manager.h"
#include "board.h"
#include "u8g2.h"
#include "ui_icons.h"
#include "ui_status.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// 本文件内使用的 UTF-8 回退辅助函数（用于保证不截断字符）
static int prev_utf8_start_local(const char* s, int idx) {
  while (idx > 0) {
    idx--;
    if (((unsigned char)s[idx] & 0xC0) != 0x80) break;
  }
  return idx;
}

static void ui_render_status_bar(const char *center_text) {
  ui_status_render(center_text);
}

void ui_render_main(int message_count, int unread_count) {
  board_display_begin();

  ui_render_status_bar(NULL);

  // 显示欢迎语或大时钟
  board_display_text(30, 35, "BIPI PAGER");

  // 显示消息计数，使用图标
  char msg_info[64];
  if (message_count > 0) {
    // 使用邮件图标
    board_display_set_font(u8g2_font_open_iconic_email_1x_t);
    board_display_glyph(10, 45, ICON_EMAIL_1X); // 邮件图标
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    
    snprintf(msg_info, sizeof(msg_info), " %d (未读: %d)", message_count, unread_count);
    board_display_text(22, 55, msg_info);
  } else {
    // 使用信息图标表示无消息
    board_display_set_font(u8g2_font_open_iconic_other_1x_t);
    board_display_glyph(10, 45, ICON_INFO_1X); // 信息图标
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    board_display_text(22, 55, "暂无消息");
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
      if (c < 0x80) char_len = 1;
      else if ((c & 0xE0) == 0xC0) char_len = 2;
      else if ((c & 0xF0) == 0xE0) char_len = 3;
      else if ((c & 0xF8) == 0xF0) char_len = 4;

      if (pos + char_len >= (int)sizeof(line_buf) - 1) break;
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
      if (clen <= 0) clen = 1;
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
  board_display_end(); // 清屏
}
