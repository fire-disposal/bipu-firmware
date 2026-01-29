#include "ui_render.h"
#include "ble_manager.h"
#include "board.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static size_t ui_get_utf8_safe_len(const char *text, size_t max_bytes) {
  size_t i = 0;
  while (text[i] != '\0' && i < max_bytes) {
    size_t char_len = 0;
    unsigned char c = (unsigned char)text[i];

    if (c < 0x80)
      char_len = 1;
    else if ((c & 0xE0) == 0xC0)
      char_len = 2;
    else if ((c & 0xF0) == 0xE0)
      char_len = 3;
    else if ((c & 0xF8) == 0xF0)
      char_len = 4;
    else
      char_len = 1; // 无效字符，按1字节处理

    if (i + char_len > max_bytes) {
      break; // 放不下了
    }

    i += char_len;
  }
  return i;
}

static void ui_draw_battery(int x, int y) {
  uint8_t pct = board_battery_percent();
  if (pct > 100)
    pct = 100;

  // 电池外框 (18x9)
  board_display_rect(x, y + 2, 18, 9, false);
  // 正极头 (2x4)
  board_display_rect(x + 18, y + 4, 2, 5, true);

  // 电量填充
  // 内部宽14px
  int fill_w = (14 * pct) / 100;
  if (fill_w > 0) {
    board_display_rect(x + 2, y + 4, fill_w, 5, true);
  }
  
  // 充电状态图标 - 只在充电时显示
  if (board_battery_is_charging()) {
    // 在电池左侧显示充电图标 "⚡" 或者用简单符号 "+"
    board_display_text(x - 8, y + 2, "+");
  }
}

static void ui_render_status_bar(const char *center_text) {
  // 绘制分割线
  board_display_rect(0, 12, 128, 1, true);

  // 1. 左侧：BLE状态图标
  bool connected = ble_manager_is_connected();
  if (connected) {
    board_display_text(2, 10, "BT");
    board_display_rect(2, 11, 10, 1, true); // 下划线
  } else {
    board_display_text(2, 10, "-X");
  }

  // 2. 中间：系统时间/序号/页码
  char buf[32];
  const char *text = center_text;

  if (text == NULL) {
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo) {
      strftime(buf, sizeof(buf), "%H:%M", timeinfo);
      text = buf;
    }
  }

  if (text) {
    int len = strlen(text);
    int w = len * 6; // 估算宽度
    int tx = (128 - w) / 2;
    if (tx < 20)
      tx = 20;
    board_display_text(tx, 10, text);
  }

  // 3. 右侧：电池电量
  ui_draw_battery(106, 0);
}

void ui_render_main(int message_count, int unread_count) {
  board_display_begin();

  ui_render_status_bar(NULL);

  // 显示欢迎语或大时钟
  board_display_text(30, 35, "BIPI PAGER");

  // 显示消息计数
  char msg_info[64];
  if (message_count > 0) {
    snprintf(msg_info, sizeof(msg_info), "消息: %d (未读: %d)", message_count,
             unread_count);
  } else {
    snprintf(msg_info, sizeof(msg_info), "暂无消息");
  }
  board_display_text(10, 55, msg_info);

  board_display_end();
}

void ui_render_message_read(const ui_message_t *msg, int current_idx,
                            int total_count) {
  if (!msg)
    return;

  board_display_begin();

  char page_str[16];
  snprintf(page_str, sizeof(page_str), "[%d/%d]", current_idx + 1, total_count);
  ui_render_status_bar(page_str);

  // 显示发送者
  char header[64];
  snprintf(header, sizeof(header), "From: %s", msg->sender);
  board_display_text(0, 25, header);

  // 显示消息内容（简单的多行显示）
  // 这里假设每行约20个字符（中文10个）
  char line_buf[32];
  const char *p = msg->text;
  int y = 38;

  while (*p && y < 64) {
    // 计算不截断UTF-8字符的安全长度
    size_t safe_len = ui_get_utf8_safe_len(p, 20);

    strncpy(line_buf, p, safe_len);
    line_buf[safe_len] = '\0';

    board_display_text(2, y, line_buf);
    y += 12;

    p += safe_len;
  }

  board_display_end();
}

void ui_render_standby(void) {
  board_display_begin();
  board_display_end(); // 清屏
}
