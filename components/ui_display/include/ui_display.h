#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "esp_err.h"
#include "u8g2.h"
#include <stdbool.h>

/**
 * @brief 初始化显示系统（I2C + OLED + u8g2）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t ui_display_init(void);

/**
 * @brief 显示 Hello World 示例
 */
void ui_display_hello_world(void);

/**
 * @brief 获取全局 u8g2 句柄（供 AstraUI 使用）
 * @return u8g2_t* 全局句柄指针
 */
u8g2_t* ui_display_get_u8g2(void);


/**
 * @brief 运行 UI 主循环（包含 AstraUI 渲染与刷新）
 */
void ui_display_main_loop(void);

/**
 * @brief 启动 UI 系统（初始化并显示欢迎画面）
 * @return true 成功，false 失败
 */
bool ui_display_start(void);

/**
 * @brief UI 渲染任务（独立任务，避免阻塞主循环）
 * @note  可直接传给 xTaskCreate，例如：
 *        xTaskCreate(ui_render_task, "ui_render", 4096, NULL, 4, NULL);
 */
void ui_render_task(void* arg);

#endif // UI_DISPLAY_H