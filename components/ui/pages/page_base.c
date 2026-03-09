#include "page_base.h"
#include "ui_task.h"
#include "ui.h"
#include "esp_log.h"

static const char* TAG = "page_base";

void page_request_render(ui_page_base_t* page)
{
    if (page == NULL) {
        return;
    }
    
    page->needs_render = true;
    ui_task_request_redraw();
}

void page_navigate_to(ui_page_base_t* from, ui_page_base_t* to, void* params)
{
    if (from == NULL || to == NULL) {
        ESP_LOGE(TAG, "Invalid page pointer");
        return;
    }
    
    ESP_LOGD(TAG, "Navigate: %s -> %s", 
             from->name ? from->name : "unknown",
             to->name ? to->name : "unknown");
    
    // 1. 调用当前页面的 exit
    if (from->on_exit != NULL) {
        from->on_exit(from);
    }
    
    // 2. 保存导航链
    to->prev = from;
    
    // 3. 强制保存 NVS（确保页面切换前数据已持久化）
    if (ui_has_pending_saves()) {
        ui_flush_pending_saves_force();
    }
    
    // 4. 调用新页面的 enter（传递参数）
    if (to->on_enter != NULL) {
        to->on_enter(to, params);
    }
    
    // 5. 请求重绘
    page_request_render(to);
}

bool page_go_back(ui_page_base_t* current)
{
    if (current == NULL || current->prev == NULL) {
        ESP_LOGD(TAG, "No previous page to go back to");
        return false;
    }
    
    ui_page_base_t* previous = current->prev;
    
    ESP_LOGD(TAG, "Go back: %s -> %s",
             current->name ? current->name : "unknown",
             previous->name ? previous->name : "unknown");
    
    // 1. 调用当前页面的 exit
    if (current->on_exit != NULL) {
        current->on_exit(current);
    }
    
    // 2. 强制保存 NVS（返回前确保数据已持久化）
    if (ui_has_pending_saves()) {
        ui_flush_pending_saves_force();
    }
    
    // 3. 调用上一页的 enter
    if (previous->on_enter != NULL) {
        previous->on_enter(previous, NULL);
    }
    
    // 4. 清除 prev 链接（已返回）
    current->prev = NULL;
    
    // 5. 请求重绘
    page_request_render(previous);
    
    return true;
}

void page_replace_with(ui_page_base_t* from, ui_page_base_t* to, void* params)
{
    if (from == NULL || to == NULL) {
        ESP_LOGE(TAG, "Invalid page pointer");
        return;
    }
    
    ESP_LOGD(TAG, "Replace: %s -> %s (no back)",
             from->name ? from->name : "unknown",
             to->name ? to->name : "unknown");
    
    // 1. 调用当前页面的 exit
    if (from->on_exit != NULL) {
        from->on_exit(from);
    }
    
    // 2. 不保存 prev 链（直接替换）
    to->prev = NULL;
    
    // 3. 调用新页面的 enter
    if (to->on_enter != NULL) {
        to->on_enter(to, params);
    }
    
    // 4. 请求重绘
    page_request_render(to);
}

void page_init(ui_page_base_t* page)
{
    if (page == NULL) {
        return;
    }
    
    // 设置默认值
    page->needs_render = true;
    page->prev = NULL;
    
    // 调用 enter（如果已定义）
    if (page->on_enter != NULL) {
        page->on_enter(page, NULL);
    }
    
    ESP_LOGD(TAG, "Page initialized: %s", page->name ? page->name : "unknown");
}
