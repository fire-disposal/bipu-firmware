#include "pages/page_base.h"
#include "ui_render.h"
#include "ui.h"
#include "u8g2.h"
#include "esp_log.h"
#include <stdio.h>

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

static void render_about(ui_page_base_t* page)
{
    (void)page;
    board_display_begin();
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "关于设备");
    
    ui_draw_text(4, 26, "BIPI Pager");
    ui_draw_text(4, 40, "固件版本：" FW_VERSION);
    ui_draw_text(4, 54, "ESP32-C3 BLE");
    
    board_display_end();
}

static void render_unbind_confirm(ui_page_base_t* page)
{
    (void)page;
    board_display_begin();
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "解绑确认");
    
    ui_draw_text(10, 30, "确定要解绑设备吗？");
    
    ui_draw_rect(10, 45, 50, 14, false);
    ui_draw_text_centered(10, 54, 50, "确定");
    
    ui_draw_rect(70, 45, 50, 14, false);
    ui_draw_text_centered(70, 54, 50, "取消");
    
    board_display_end();
}

static void page_settings_render(ui_page_base_t* page)
{
    if (s_ctx.show_about) {
        render_about(page);
        return;
    }
    
    if (s_ctx.show_unbind_confirm) {
        render_unbind_confirm(page);
        return;
    }
    
    board_display_begin();
    
    // 标题
    ui_set_font(u8g2_font_wqy12_t_gb2312a);
    ui_draw_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "设置");
    
    // 渲染设置列表
    const int line_height = 14;
    const int start_y = 16;
    
    for (int i = 0; i < SETTING_COUNT; i++) {
        int y = start_y + i * line_height;
        
        if (i == s_ctx.selected_item) {
            // 选中项反色
            ui_set_draw_color(1);
            ui_draw_rect(0, y - 12, 128, line_height, true);
            ui_set_draw_color(0);
            
            // 亮度项显示当前值
            if (i == SETTING_BRIGHTNESS) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s: %d%%", s_setting_names[i], s_ctx.brightness);
                ui_draw_text(4, y, buf);
            } else if (i == SETTING_FLASHLIGHT) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s: %s", s_setting_names[i], s_ctx.flashlight_on ? "开" : "关");
                ui_draw_text(4, y, buf);
            } else {
                ui_draw_text(4, y, s_setting_names[i]);
            }
        } else {
            ui_set_draw_color(1);
            if (i == SETTING_BRIGHTNESS) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s: %d%%", s_setting_names[i], s_ctx.brightness);
                ui_draw_text(4, y, buf);
            } else if (i == SETTING_FLASHLIGHT) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s: %s", s_setting_names[i], s_ctx.flashlight_on ? "开" : "关");
                ui_draw_text(4, y, buf);
            } else {
                ui_draw_text(4, y, s_setting_names[i]);
            }
        }
    }
    
    board_display_end();
}

static void page_settings_on_key(ui_page_base_t* page, board_key_t key)
{
    // 关于页面按键处理
    if (s_ctx.show_about) {
        if (key == BOARD_KEY_BACK || key == BOARD_KEY_ENTER) {
            s_ctx.show_about = false;
            page_request_render(page);
        }
        return;
    }
    
    // 解绑确认页面按键处理
    if (s_ctx.show_unbind_confirm) {
        if (key == BOARD_KEY_ENTER || key == BOARD_KEY_DOWN) {
            // 确定解绑
            ble_manager_unpair();
            ui_show_toast("已解绑", 1500);
            s_ctx.show_unbind_confirm = false;
            ui_go_back_page();
        } else if (key == BOARD_KEY_BACK || key == BOARD_KEY_UP) {
            // 取消
            s_ctx.show_unbind_confirm = false;
            page_request_render(page);
        }
        return;
    }
    
    // 主设置页面按键处理
    switch (key) {
        case BOARD_KEY_BACK:
            ui_go_back_page();
            break;
            
        case BOARD_KEY_DOWN:
            if (s_ctx.selected_item < SETTING_COUNT - 1) {
                s_ctx.selected_item++;
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_UP:
            if (s_ctx.selected_item > 0) {
                s_ctx.selected_item--;
                page_request_render(page);
            }
            break;
            
        case BOARD_KEY_ENTER:
            switch (s_ctx.selected_item) {
                case SETTING_BRIGHTNESS:
                    // 循环切换亮度
                    uint8_t new_brightness = (s_ctx.brightness + 10) % 91 + 10;  // 10-100%
                    ui_set_brightness(new_brightness);
                    s_ctx.brightness = new_brightness;
                    page_request_render(page);
                    break;
                    
                case SETTING_FLASHLIGHT:
                    ui_toggle_flashlight();
                    s_ctx.flashlight_on = ui_is_flashlight_on();
                    page_request_render(page);
                    break;
                    
                case SETTING_UNBIND:
                    s_ctx.show_unbind_confirm = true;
                    page_request_render(page);
                    break;
                    
                case SETTING_RESTART:
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
            
        default:
            break;
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
