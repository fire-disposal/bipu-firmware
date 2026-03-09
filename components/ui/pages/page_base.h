#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file page_base.h
 * @brief 页面基类定义
 * 
 * 设计思想：
 * 1. 统一页面生命周期管理
 * 2. 提供通用工具函数
 * 3. 强制代码风格一致
 * 4. 支持页面栈导航
 */

/* ================== 前向声明 ================== */

struct ui_page_base;

/* ================== 页面回调函数类型 ================== */

/**
 * @brief 页面进入回调
 * @param page 页面对象指针
 * @param params 参数指针（可选，可为 NULL）
 */
typedef void (*page_on_enter_cb)(struct ui_page_base* page, void* params);

/**
 * @brief 页面退出回调
 * @param page 页面对象指针
 */
typedef void (*page_on_exit_cb)(struct ui_page_base* page);

/**
 * @brief 页面更新回调
 * @param page 页面对象指针
 * @param delta_ms 距上次更新的时间间隔（毫秒）
 */
typedef void (*page_update_cb)(struct ui_page_base* page, uint32_t delta_ms);

/**
 * @brief 页面渲染回调
 * @param page 页面对象指针
 */
typedef void (*page_render_cb)(struct ui_page_base* page);

/**
 * @brief 按键处理回调
 * @param page 页面对象指针
 * @param key 按键值
 */
typedef void (*page_on_key_cb)(struct ui_page_base* page, board_key_t key);

/* ================== 页面基类结构 ================== */

typedef struct ui_page_base {
    /* === 虚函数表（子类必须实现） === */
    page_on_enter_cb  on_enter;       /**< 进入页面时调用 */
    page_on_exit_cb   on_exit;        /**< 退出页面时调用 */
    page_update_cb    update;         /**< 周期性更新（可选） */
    page_render_cb    render;         /**< 渲染页面（必须） */
    page_on_key_cb    on_key;         /**< 按键处理（可选） */
    
    /* === 页面属性 === */
    const char* name;                 /**< 页面名称（用于调试） */
    uint32_t    update_interval;      /**< 更新间隔（ms），0=不需要 update */
    bool        needs_render;         /**< 脏标记（需要重绘） */
    
    /* === 页面上下文 === */
    void* context;                    /**< 指向子页面的私有数据 */
    
    /* === 页面栈支持 === */
    struct ui_page_base* prev;        /**< 前一页面（用于返回） */
    
} ui_page_base_t;

/* ================== 基类提供的工具函数 ================== */

/**
 * @brief 请求重绘页面
 * 
 * 调用后会自动设置 needs_render = true 并通知 GUI 任务
 * 
 * @param page 页面对象指针
 */
void page_request_render(ui_page_base_t* page);

/**
 * @brief 导航到新页面（带参数传递）
 * 
 * 自动处理：
 * 1. 调用当前页面的 on_exit
 * 2. 保存当前页面到 prev
 * 3. 调用新页面的 on_enter
 * 
 * @param from 当前页面对象指针
 * @param to 目标页面对象指针
 * @param params 传递给新页面的参数（可选）
 */
void page_navigate_to(ui_page_base_t* from, ui_page_base_t* to, void* params);

/**
 * @brief 返回上一页
 * 
 * 自动处理：
 * 1. 调用当前页面的 on_exit
 * 2. 恢复到 prev 页面
 * 3. 调用上一页的 on_enter
 * 
 * @param current 当前页面对象指针
 * @return true 成功返回
 * @return false 没有上一页
 */
bool page_go_back(ui_page_base_t* current);

/**
 * @brief 替换当前页面（不保存到页面栈）
 * 
 * 用于：
 * - 模态对话框
 * - 临时页面
 * - 不想保留返回路径的场景
 * 
 * @param from 当前页面对象指针
 * @param to 目标页面对象指针
 * @param params 参数（可选）
 */
void page_replace_with(ui_page_base_t* from, ui_page_base_t* to, void* params);

/**
 * @brief 初始化页面对象
 * 
 * 设置默认值并调用 on_enter
 * 
 * @param page 页面对象指针
 */
void page_init(ui_page_base_t* page);

#ifdef __cplusplus
}
#endif
