#ifndef UI_CUSTOM_H
#define UI_CUSTOM_H

/**
 * @brief 自定义页面初始化
 */
void my_custom_page_init(void);

/**
 * @brief 自定义页面主循环（绘制内容）
 */
void my_custom_page_loop(void);

/**
 * @brief 自定义页面退出
 */
void my_custom_page_exit(void);

/**
 * @brief 注册所有自定义页面到 UI 内容
 */
void ui_custom_register_pages(void);

#endif // UI_CUSTOM_H