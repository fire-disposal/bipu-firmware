#include "ui_page.h"
#include "ui.h"
#include "ui_render.h"
#include "ui_text.h"
#include "board.h"
#include "u8g2.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PAGE_SETTINGS";

/* ================== 设置选项定义 ================== */
typedef enum {
    SETTING_BRIGHTNESS,    // 亮度调节
    SETTING_FLASHLIGHT,    // 手电筒开关
    SETTING_LOCKSCREEN,    // 锁屏立即进入屏保
    SETTING_RESTART,       // 重启系统
    SETTING_ABOUT,         // 关于设备
    SETTING_BACK,          // 返回
    SETTING_COUNT
} setting_item_t;

static const char* s_setting_names[] = {
    "屏幕亮度",
    "手电筒",
    "锁屏",
    "重启",
    "关于",
    "← 返回"
};

static int s_selected_item = 0;
static bool s_editing = false;  // 是否正在编辑某个设置项
static bool s_show_about = false;  // 显示关于页面

// 滚动相关参数
#define ITEMS_PER_PAGE 4  // 每页显示的设置项数量
#define LINE_HEIGHT 12    // 每行高度
#define CONTENT_START_Y 24 // 内容起始Y坐标

/* ================== 页面生命周期 ================== */
static void page_on_enter(void) {
    ESP_LOGD(TAG, "Entering Settings Page");
    s_selected_item = 0;
    s_editing = false;
    s_show_about = false;
}

static void page_on_exit(void) {
    ESP_LOGD(TAG, "Exiting Settings Page");
    s_editing = false;
    s_show_about = false;
}

/* ================== 渲染关于页面 ================== */
static void render_about(void) {
    board_display_begin();
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // 标题栏
    board_display_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "关于设备");
    
    // 设备信息
    board_display_text(4, 26, "BIPI Pager v1.0");
    board_display_text(4, 40, "ESP32-C3 BLE");
    
    board_display_end();
}

/* ================== 渲染设置页面 ================== */
static void render_settings(void) {
    board_display_begin();
    board_display_set_font(u8g2_font_wqy12_t_gb2312a);
    
    // 标题栏
    board_display_rect(0, 12, 128, 1, true);
    ui_draw_text_centered(0, 10, 128, "设置");
    
    // 计算当前页码和起始项
    int page = s_selected_item / ITEMS_PER_PAGE;
    int start_item = page * ITEMS_PER_PAGE;
    int end_item = start_item + ITEMS_PER_PAGE;
    if (end_item > SETTING_COUNT) end_item = SETTING_COUNT;
    
    // 显示当前页的选项
    int y = CONTENT_START_Y;
    for (int i = start_item; i < end_item; i++) {
        // 选中标记
        if (i == s_selected_item) {
            // 选中行反色：白色背景 + 黑色文字
            board_display_set_draw_color(1);
            board_display_rect(0, y - LINE_HEIGHT + 2, 128, LINE_HEIGHT, true);
            board_display_set_font_mode(1);
            board_display_set_draw_color(0);
            board_display_text(2, y, "›");
        }
        
        // 设置项名称
        board_display_text(12, y, s_setting_names[i]);
        
        // 显示当前值
        char value_str[32];
        switch (i) {
            case SETTING_BRIGHTNESS: {
                uint8_t brightness = ui_get_brightness();
                if (s_editing && i == s_selected_item) {
                    // 编辑模式：显示调节指示
                    snprintf(value_str, sizeof(value_str), "‹%d%%›", brightness);
                } else {
                    snprintf(value_str, sizeof(value_str), "%d%%", brightness);
                }
                int tw = board_display_text_width(value_str);
                board_display_text(124 - tw, y, value_str);
                break;
            }
            case SETTING_FLASHLIGHT: {
                bool on = ui_is_flashlight_on();
                const char* state = on ? "开" : "关";
                int tw = board_display_text_width(state);
                board_display_text(124 - tw, y, state);
                break;
            }
            case SETTING_ABOUT:
            case SETTING_LOCKSCREEN:
            case SETTING_RESTART:
                // 无值显示
                break;
            case SETTING_BACK:
                // 不显示值
                break;
        }
        
        // 恢复正常绘制模式
        if (i == s_selected_item) {
            board_display_set_draw_color(1);
            board_display_set_font_mode(0);
        }
        
        y += LINE_HEIGHT;
    }
    
    board_display_end();
}

static void tick(void) {
    if (s_show_about) {
        render_about();
    } else {
        render_settings();
    }
}

/* ================== 按键处理 ================== */
static void on_key(board_key_t key) {
    ESP_LOGD(TAG, "Settings key: %d, editing: %d, about: %d", key, s_editing, s_show_about);
    
    // 关于页面：任意键返回
    if (s_show_about) {
        s_show_about = false;
        return;
    }
    
    if (s_editing) {
        // 编辑模式
        switch (s_selected_item) {
            case SETTING_BRIGHTNESS: {
                uint8_t brightness = ui_get_brightness();
                if (key == BOARD_KEY_UP) {
                    if (brightness < 100) {
                        brightness += 10;
                        if (brightness > 100) brightness = 100;
                        ui_set_brightness(brightness);
                    }
                } else if (key == BOARD_KEY_DOWN) {
                    if (brightness > 10) {
                        brightness -= 10;
                        if (brightness < 10) brightness = 10;
                        ui_set_brightness(brightness);
                    }
                } else if (key == BOARD_KEY_ENTER || key == BOARD_KEY_BACK) {
                    s_editing = false;
                }
                break;
            }
            default:
                s_editing = false;
                break;
        }
    } else {
        // 选择模式
        switch (key) {
            case BOARD_KEY_UP:
                s_selected_item--;
                if (s_selected_item < 0) s_selected_item = SETTING_COUNT - 1;
                break;
                
            case BOARD_KEY_DOWN:
                s_selected_item++;
                if (s_selected_item >= SETTING_COUNT) s_selected_item = 0;
                break;
                
            case BOARD_KEY_ENTER:
                switch (s_selected_item) {
                    case SETTING_BRIGHTNESS:
                        s_editing = true;
                        break;
                    case SETTING_FLASHLIGHT:
                        ui_toggle_flashlight();
                        break;
                    case SETTING_LOCKSCREEN:
                        // 立即进入屏保/锁屏
                        ui_enter_standby();
                        break;
                    case SETTING_RESTART:
                        // 执行系统重启
                        ui_system_restart();
                        break;
                    case SETTING_ABOUT:
                        s_show_about = true;
                        break;
                    case SETTING_BACK:
                        ui_change_page(UI_STATE_MAIN);
                        break;
                }
                break;
                
            case BOARD_KEY_BACK:
                ui_change_page(UI_STATE_MAIN);
                break;
                
            default:
                break;
        }
    }
}

const ui_page_t page_settings = {
    .on_enter = page_on_enter,
    .on_exit = page_on_exit,
    .tick = tick,
    .on_key = on_key,
};
