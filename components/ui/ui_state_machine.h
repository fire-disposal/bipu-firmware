#pragma once

#include "pages/page_base.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_state_machine.h
 * @brief UI 状态机（管理页面导航）
 * 
 * 功能：
 * 1. 页面导航（带参数传递）
 * 2. 页面栈管理（支持返回）
 * 3. 状态转换守卫
 * 4. 生命周期自动调用
 */

/* ================== 配置常量 ================== */

/** 最大页面栈深度 */
#define UI_STACK_MAX_DEPTH  8

/* ================== 状态机结构 ================== */

typedef struct {
    ui_page_base_t* current_page;             /**< 当前页面 */
    ui_page_base_t* page_stack[UI_STACK_MAX_DEPTH];  /**< 页面栈 */
    uint8_t         stack_depth;              /**< 栈深度 */
    bool            locked;                   /**< 锁定标志（防止重入） */
} ui_state_machine_t;

/* ================== 状态机接口 ================== */

/**
 * @brief 获取状态机单例
 * @return ui_state_machine_t* 状态机实例指针
 */
ui_state_machine_t* ui_state_machine_get_instance(void);

/**
 * @brief 初始化状态机
 * @return esp_err_t 
 *   - ESP_OK: 初始化成功
 *   - ESP_FAIL: 初始化失败
 */
esp_err_t ui_state_machine_init(void);

/**
 * @brief 导航到新页面（压栈）
 * 
 * 自动处理：
 * 1. 当前页面压入栈
 * 2. 调用旧页面 on_exit
 * 3. 调用新页面 on_enter
 * 
 * @param page 目标页面
 * @param params 参数（可选）
 * @return true 成功
 * @return false 失败（栈满或锁定）
 */
bool ui_state_machine_navigate(ui_page_base_t* page, void* params);

/**
 * @brief 返回上一页（出栈）
 * @return true 成功
 * @return false 失败（栈空或锁定）
 */
bool ui_state_go_back(void);

/**
 * @brief 替换当前页面（不压栈）
 * 
 * 用于模态对话框等场景
 * 
 * @param page 目标页面
 * @param params 参数（可选）
 * @return true 成功
 * @return false 失败
 */
bool ui_replace_page(ui_page_base_t* page, void* params);

/**
 * @brief 清空栈并跳转到首页
 * 
 * 用于：
 * - 重启 UI 流程
 * - 返回首页
 * 
 * @param home_page 首页页面
 * @return true 成功
 * @return false 失败
 */
bool ui_navigate_home(ui_page_base_t* home_page);

/**
 * @brief 获取当前页面
 * @return ui_page_base_t* 当前页面，NULL 表示未初始化
 */
ui_page_base_t* ui_get_current_page(void);

/**
 * @brief 检查是否可以导航
 * @return true 可以导航
 * @return false 锁定中或栈满
 */
bool ui_can_navigate(void);

/**
 * @brief 锁定状态机（防止重入）
 */
void ui_state_machine_lock(void);

/**
 * @brief 解锁状态机
 */
void ui_state_machine_unlock(void);

/**
 * @brief 获取当前栈深度
 * @return uint8_t 栈深度
 */
uint8_t ui_get_stack_depth(void);

/**
 * @brief 打印调试信息（页面栈）
 */
void ui_state_machine_debug_print(void);

#ifdef __cplusplus
}
#endif
