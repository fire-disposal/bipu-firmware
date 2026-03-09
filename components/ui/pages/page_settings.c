#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "u8g2.h"
#include "esp_log.h"
#include <stdio.h>
#include "ble_manager.h"

static const char* TAG = "page_settings";

#define FW_VERSION "v1.2.0"

/* ================== 设置选项定义 ================== */
typedef enum {
    SETTING_BRIGHTNESS,
    SETTING_FLASHLIGHT,
    SETTING_LOCKSCREEN,
    SETTING_UNBIND,
    SETTING_RESTART,
    SETTING_ABOUT,
    SETTING_BACK,
    SETTING_COUNT
} setting_item_t;

static const char* s_setting_names[] = {
    "屏幕亮度",
    "手电筒",
    "锁屏",
    "解绑设备",
    "重启",
    "关于",
    "← 返回"
};

/* 页面配置常量（参考旧版本最佳视觉效果） */
#define ITEMS_PER_PAGE     4     /* 每页显示数量 */
#define LINE_HEIGHT        12    /* 行高 */
#define CONTENT_START_Y    24    /* 内容起始 Y 坐标 */

/* ================== 页面上下文 ================== */
typedef struct {
    int selected_item;
    bool editing;
    bool show_about;
    bool show_unbind_confirm;
    uint8_t brightness;
    bool flashlight_on;
} settings_page_context_t;

static settings_page_context_t s_ctx = {0};

/* ================== 辅助渲染函数 ================== */

static void render_about_page(void)
{
    board_display_begin();
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // 标题栏
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "关于设备");
    
    // 设备信息
    ui_draw_text(4, 26, "BIPI Pager");
    ui_draw_text(4, 40, "固件版本：" FW_VERSION);
    ui_draw_text(4, 54, "ESP32-C3 BLE");
    
    board_display_end();
}

static void render_unbind_confirm_page(void)
{
    board_display_begin();
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // 标题栏
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "解绑确认");
    
    // 确认信息
    ui_draw_text(4, 26, "确定要解绑设备吗？");
    ui_draw_text(4, 40, "解绑后需要重新绑定");
    ui_draw_text(4, 54, "才能使用蓝牙功能");
    
    // 操作提示
    ui_draw_text(4, 68, "确认：上键");
    ui_draw_text(64, 68, "取消：下键");
    
    board_display_end();
}

static void render_settings_page(void)
{
    board_display_begin();
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // 标题栏
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "设置");
    
    // 计算当前页码和起始项（分页显示）
    int page = s_ctx.selected_item / ITEMS_PER_PAGE;
    int start_item = page * ITEMS_PER_PAGE;
    int end_item = start_item + ITEMS_PER_PAGE;
    if (end_item > SETTING_COUNT) end_item = SETTING_COUNT;
    
    // 渲染当前页的选项
    int y = CONTENT_START_Y;
    for (int i = start_item; i < end_item; i++) {
        // 选中标记：使用 › 符号 + 反色背景
        if (i == s_ctx.selected_item) {
            // 选中行反色：白色背景 + 黑色文字
            ui_set_draw_color(1);
            ui_draw_rect(0, y - LINE_HEIGHT + 2, 128, LINE_HEIGHT, true);
            ui_set_draw_color(0);
            ui_draw_text(2, y, "›");
        }
        
        // 设置项名称
        ui_draw_text(12, y, s_setting_names[i]);
        
        // 显示当前值
        char value_str[32];
        switch (i) {
            case SETTING_BRIGHTNESS: {
                if (s_ctx.editing && i == s_ctx.selected_item) {
                    // 编辑模式：显示调节指示
                    snprintf(value_str, sizeof(value_str), "‹%d%%›", s_ctx.brightness);
                } else {
                    snprintf(value_str, sizeof(value_str), "%d%%", s_ctx.brightness);
                }
                int tw = board_display_text_width(value_str);
                ui_draw_text(124 - tw, y, value_str);
                break;
            }
            case SETTING_FLASHLIGHT: {
                const char* state = s_ctx.flashlight_on ? "开" : "关";
                int tw = board_display_text_width(state);
                ui_draw_text(124 - tw, y, state);
                break;
            }
            case SETTING_LOCKSCREEN:
            case SETTING_UNBIND:
            case SETTING_RESTART:
            case SETTING_ABOUT:
            case SETTING_BACK:
                // 无值显示
                break;
        }
        
        // 恢复正常绘制模式
        if (i == s_ctx.selected_item) {
            ui_set_draw_color(1);
        }
        
        y += LINE_HEIGHT;
    }
    
    board_display_end();
}

/* ================== 页面生命周期回调 ================== */

static void page_settings_on_enter(ui_page_base_t* page, void* params)
{
    (void)params;
    ESP_LOGD(TAG, "Entering Settings Page");
    s_ctx.selected_item = 0;
    s_ctx.editing = false;
    s_ctx.show_about = false;
    s_ctx.show_unbind_confirm = false;
    s_ctx.brightness = ui_get_brightness();
    s_ctx.flashlight_on = ui_is_flashlight_on();
    page_request_render(page);
}

static void page_settings_on_exit(ui_page_base_t* page)
{
    (void)page;
    ESP_LOGD(TAG, "Exiting Settings Page");
    s_ctx.editing = false;
    s_ctx.show_about = false;
    s_ctx.show_unbind_confirm = false;
}

static void page_settings_render(ui_page_base_t* page)
{
    (void)page;
    
    if (s_ctx.show_about) {
        render_about_page();
        return;
    }
    
    if (s_ctx.show_unbind_confirm) {
        render_unbind_confirm_page();
        return;
    }
    
    render_settings_page();
}

static void page_settings_on_key(ui_page_base_t* page, board_key_t key)
{
    ESP_LOGD(TAG, "Settings key: %d, editing: %d, about: %d, unbind: %d", 
             key, s_ctx.editing, s_ctx.show_about, s_ctx.show_unbind_confirm);
    
    // 关于页面：任意键返回
    if (s_ctx.show_about) {
        s_ctx.show_about = false;
        page_request_render(page);
        return;
    }
    
    // 解绑确认页面
    if (s_ctx.show_unbind_confirm) {
        if (key == BOARD_KEY_UP) {
            // 确认解绑
            ESP_LOGI(TAG, "用户确认解绑设备");
            ble_manager_unpair();
            s_ctx.show_unbind_confirm = false;
            ui_show_toast("解绑成功，即将重启", 2000);
            ui_go_back_page();
        } else if (key == BOARD_KEY_DOWN || key == BOARD_KEY_BACK) {
            // 取消解绑
            ESP_LOGI(TAG, "用户取消解绑");
            s_ctx.show_unbind_confirm = false;
            ui_show_toast("已取消", 1200);
            page_request_render(page);
        }
        return;
    }
    
    if (s_ctx.editing) {
        // 编辑模式
        switch (s_ctx.selected_item) {
            case SETTING_BRIGHTNESS: {
                if (key == BOARD_KEY_UP) {
                    if (s_ctx.brightness < 100) {
                        s_ctx.brightness += 10;
                        if (s_ctx.brightness > 100) s_ctx.brightness = 100;
                        ui_set_brightness(s_ctx.brightness);
                        char buf[20];
                        snprintf(buf, sizeof(buf), "亮度：%d%%", s_ctx.brightness);
                        ui_show_toast(buf, 1200);
                        page_request_render(page);
                    }
                } else if (key == BOARD_KEY_DOWN) {
                    if (s_ctx.brightness > 10) {
                        s_ctx.brightness -= 10;
                        if (s_ctx.brightness < 10) s_ctx.brightness = 10;
                        ui_set_brightness(s_ctx.brightness);
                        char buf[20];
                        snprintf(buf, sizeof(buf), "亮度：%d%%", s_ctx.brightness);
                        ui_show_toast(buf, 1200);
                        page_request_render(page);
                    }
                } else if (key == BOARD_KEY_ENTER || key == BOARD_KEY_BACK) {
                    // 退出编辑模式
                    s_ctx.editing = false;
                }
                break;
            }
            default:
                s_ctx.editing = false;
                break;
        }
    } else {
        // 选择模式
        switch (key) {
            case BOARD_KEY_UP:
                s_ctx.selected_item--;
                if (s_ctx.selected_item < 0) s_ctx.selected_item = SETTING_COUNT - 1;
                page_request_render(page);
                break;
                
            case BOARD_KEY_DOWN:
                s_ctx.selected_item++;
                if (s_ctx.selected_item >= SETTING_COUNT) s_ctx.selected_item = 0;
                page_request_render(page);
                break;
                
            case BOARD_KEY_ENTER:
                switch (s_ctx.selected_item) {
                    case SETTING_BRIGHTNESS:
                        // 进入亮度编辑模式
                        s_ctx.editing = true;
                        break;
                    case SETTING_FLASHLIGHT:
                        ui_toggle_flashlight();
                        s_ctx.flashlight_on = ui_is_flashlight_on();
                        ui_show_toast(s_ctx.flashlight_on ? "手电筒 已开启" : "手电筒 已关闭", 1500);
                        page_request_render(page);
                        break;
                    case SETTING_LOCKSCREEN:
                        // 立即进入屏保/锁屏
                        ui_show_toast("锁屏", 800);
                        // 这里可以添加锁屏逻辑
                        break;
                    case SETTING_UNBIND:
                        // 显示解绑确认页面
                        s_ctx.show_unbind_confirm = true;
                        page_request_render(page);
                        break;
                    case SETTING_RESTART:
                        ui_show_toast("重启中...", 500);
                        ui_system_restart();
                        break;
                    case SETTING_ABOUT:
                        s_ctx.show_about = true;
                        page_request_render(page);
                        break;
                    case SETTING_BACK:
                        ui_go_back_page();
                        break;
                }
                break;
                
            case BOARD_KEY_BACK:
                ui_go_back_page();
                break;
                
            case BOARD_KEY_BACK_LONG:
                // 长按返回键：手电筒快速开关
                if (ui_is_flashlight_on()) {
                    ui_toggle_flashlight();
                    s_ctx.flashlight_on = false;
                    ui_show_toast("手电筒 关闭", 1000);
                } else {
                    ui_toggle_flashlight();
                    s_ctx.flashlight_on = true;
                    ui_show_toast("手电筒 开启", 1000);
                }
                page_request_render(page);
                break;
                
            default:
                break;
        }
    }
}

/* ================== 页面对象定义 ================== */

static ui_page_base_t s_settings_page = {
    .name = "Settings",
    .on_enter = page_settings_on_enter,
    .on_exit = page_settings_on_exit,
    .update = NULL,
    .render = page_settings_render,
    .on_key = page_settings_on_key,
    .update_interval = 0,
    .context = &s_ctx,
};

/* ================== 公开接口 ================== */

ui_page_base_t* ui_get_settings_page(void)
{
    return &s_settings_page;
}
