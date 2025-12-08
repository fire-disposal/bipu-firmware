/**
 * @file astraui_usage.h
 * @brief AstraUI Lite 使用示例和说明
 * 
 * 这个文件展示了如何在 ESP-IDF 项目中集成和使用 AstraUI Lite 与 u8g2
 */

#ifndef ASTRA_UI_USAGE_H
#define ASTRA_UI_USAGE_H

#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AstraUI Lite 系统
 * @param u8g2 已经初始化的 u8g2 实例
 * @return 0 成功，-1 失败
 * 
 * @note 必须在 u8g2 初始化完成后调用
 */
int astraui_lite_init(u8g2_t *u8g2);

/**
 * @brief 创建示例菜单
 * @return 0 成功，-1 失败
 * 
 * @note 这个函数创建一个包含各种菜单项的示例菜单
 */
int astraui_lite_create_demo_menu(void);

/**
 * @brief 主循环处理函数
 * @note 应该在主循环中调用这个函数来处理 AstraUI Lite 的逻辑
 */
void astraui_lite_loop(void);

#ifdef __cplusplus
}
#endif

#endif // ASTRA_UI_USAGE_H