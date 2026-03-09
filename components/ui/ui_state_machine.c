#include "ui_state_machine.h"
#include "ui_task.h"
#include "esp_log.h"

static const char* TAG = "ui_state_machine";

/* ================== 单例实现 ================== */

static ui_state_machine_t s_instance = {0};

ui_state_machine_t* ui_state_machine_get_instance(void)
{
    return &s_instance;
}

/* ================== 公开 API 实现 ================== */

esp_err_t ui_state_machine_init(void)
{
    if (s_instance.current_page != NULL) {
        ESP_LOGW(TAG, "State machine already initialized");
        return ESP_OK;
    }
    
    // 清零初始化
    memset(&s_instance, 0, sizeof(s_instance));
    s_instance.locked = false;
    
    ESP_LOGI(TAG, "State machine initialized (max_depth=%d)", UI_STACK_MAX_DEPTH);
    return ESP_OK;
}

bool ui_navigate(ui_page_base_t* page, void* params)
{
    if (page == NULL) {
        ESP_LOGE(TAG, "Navigate to NULL page");
        return false;
    }
    
    if (s_instance.locked) {
        ESP_LOGW(TAG, "State machine locked, navigation deferred");
        return false;
    }
    
    if (s_instance.current_page == NULL) {
        // 首次导航（设置首页）
        s_instance.current_page = page;
        page_init(page);
        ESP_LOGI(TAG, "Initial page set: %s", page->name ? page->name : "unknown");
        return true;
    }
    
    // 检查栈深度
    if (s_instance.stack_depth >= UI_STACK_MAX_DEPTH) {
        ESP_LOGW(TAG, "Page stack full (%d levels)", UI_STACK_MAX_DEPTH);
        return false;
    }
    
    ui_page_base_t* old_page = s_instance.current_page;
    
    // 压栈
    s_instance.page_stack[s_instance.stack_depth++] = old_page;
    
    // 导航
    page_navigate_to(old_page, page, params);
    
    // 更新当前页面
    s_instance.current_page = page;
    
    ESP_LOGD(TAG, "Navigation complete, stack_depth=%d", s_instance.stack_depth);
    return true;
}

bool ui_go_back(void)
{
    if (s_instance.locked) {
        ESP_LOGW(TAG, "State machine locked, go_back deferred");
        return false;
    }
    
    if (s_instance.current_page == NULL || s_instance.stack_depth == 0) {
        ESP_LOGD(TAG, "Cannot go back: no previous page");
        return false;
    }
    
    // 出栈
    ui_page_base_t* previous = s_instance.page_stack[--s_instance.stack_depth];
    
    // 调用基类的 page_go_back
    if (!page_go_back(s_instance.current_page)) {
        return false;
    }
    
    // 更新当前页面
    s_instance.current_page = previous;
    
    ESP_LOGD(TAG, "Go back complete, stack_depth=%d", s_instance.stack_depth);
    return true;
}

bool ui_replace_page(ui_page_base_t* page, void* params)
{
    if (page == NULL) {
        ESP_LOGE(TAG, "Replace with NULL page");
        return false;
    }
    
    if (s_instance.locked) {
        ESP_LOGW(TAG, "State machine locked, replace deferred");
        return false;
    }
    
    if (s_instance.current_page == NULL) {
        ESP_LOGE(TAG, "No current page to replace");
        return false;
    }
    
    ui_page_base_t* old_page = s_instance.current_page;
    
    // 替换（不压栈）
    page_replace_with(old_page, page, params);
    
    // 更新当前页面
    s_instance.current_page = page;
    
    ESP_LOGD(TAG, "Page replaced: %s", page->name ? page->name : "unknown");
    return true;
}

bool ui_navigate_home(ui_page_base_t* home_page)
{
    if (home_page == NULL) {
        ESP_LOGE(TAG, "Home page is NULL");
        return false;
    }
    
    if (s_instance.locked) {
        ESP_LOGW(TAG, "State machine locked, navigate_home deferred");
        return false;
    }
    
    // 清空栈
    s_instance.stack_depth = 0;
    
    // 直接设置首页（不调用 exit/enter）
    home_page->prev = NULL;
    home_page->needs_render = true;
    s_instance.current_page = home_page;
    
    // 请求重绘
    page_request_render(home_page);
    
    ESP_LOGI(TAG, "Navigated to home: %s", home_page->name ? home_page->name : "unknown");
    return true;
}

ui_page_base_t* ui_get_current_page(void)
{
    return s_instance.current_page;
}

bool ui_can_navigate(void)
{
    return !s_instance.locked && 
           s_instance.stack_depth < UI_STACK_MAX_DEPTH;
}

void ui_state_machine_lock(void)
{
    s_instance.locked = true;
    ESP_LOGD(TAG, "State machine locked");
}

void ui_state_machine_unlock(void)
{
    s_instance.locked = false;
    ESP_LOGD(TAG, "State machine unlocked");
}

uint8_t ui_get_stack_depth(void)
{
    return s_instance.stack_depth;
}

void ui_state_machine_debug_print(void)
{
    ESP_LOGI(TAG, "=== UI State Machine Debug ===");
    ESP_LOGI(TAG, "Current page: %s", 
             s_instance.current_page ? 
             (s_instance.current_page->name ? s_instance.current_page->name : "unnamed") : 
             "NULL");
    ESP_LOGI(TAG, "Stack depth: %d / %d", s_instance.stack_depth, UI_STACK_MAX_DEPTH);
    
    if (s_instance.stack_depth > 0) {
        ESP_LOGI(TAG, "Page stack (bottom to top):");
        for (int i = 0; i < s_instance.stack_depth; i++) {
            ui_page_base_t* p = s_instance.page_stack[i];
            ESP_LOGI(TAG, "  [%d] %s", i, p->name ? p->name : "unnamed");
        }
    }
    
    ESP_LOGI(TAG, "Locked: %s", s_instance.locked ? "yes" : "no");
    ESP_LOGI(TAG, "==============================");
}
