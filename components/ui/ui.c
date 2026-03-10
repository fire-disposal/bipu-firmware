#include "ui.h"
#include "ui_page.h"
#include "ui_types.h"
#include "ui_render.h"
#include "board.h"
#include "storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* UI_TAG = "ui_manager";

/* ================== UI 互斥锁 ================== */
// 保护所有 UI 状态（state, messages, indices, page vars）
// 避免 app_task(ui_on_key) 与 gui_task(ui_tick) 之间的竞态
static SemaphoreHandle_t s_ui_mutex = NULL;

static inline bool ui_lock(void) {
    if (s_ui_mutex == NULL) return true;
    return xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(200)) == pdTRUE;
}

static inline void ui_unlock(void) {
    if (s_ui_mutex != NULL) {
        xSemaphoreGive(s_ui_mutex);
    }
}

#define STANDBY_TIMEOUT_MS 30000  /* 30 秒后进入屏保 */
#define BLACKSCREEN_TIMEOUT_MS 30000 /* 屏保播放 30 秒后进入黑屏 */
#define DEFAULT_BRIGHTNESS 100

/* ================== 延迟 NVS 保存状态 ================== */
/* ui_delete_current_message / ui_set_brightness 在 ui_on_key 持锁时被调用，
 * 不可在锁内直接写 NVS（约 10-50ms 会阻塞 ui_tick / ui_on_key）。
 * 改为：锁内快照数据 + 设标志，由 app_loop 调用 ui_flush_pending_saves() 完成写入。 */
static bool s_deferred_msg_save = false;
static bool s_deferred_brightness_save = false;
static storage_message_t s_save_snap[MAX_MESSAGES];
static int  s_save_count;
static int  s_save_idx;
static uint8_t s_save_brightness;

/* ================== Toast 状态 ================== */
#define TOAST_MSG_MAX 64
static char     s_toast_msg[TOAST_MSG_MAX];
static bool     s_toast_visible   = false;
static uint32_t s_toast_expire_ms = 0;   /* 0 = 不自动消失 */

/* 预刷新钩子：由 board_display_end → SendBuffer 之前调用 */
static void toast_pre_flush_cb(void) {
    if (s_toast_visible) {
        ui_render_toast_overlay(s_toast_msg);
    }
}

/* ================== 外部页面引用 ================== */
extern const ui_page_t page_main;
extern const ui_page_t page_list;
extern const ui_page_t page_message;
extern const ui_page_t page_settings;

static const ui_page_t* s_pages[] = {
    [UI_STATE_MAIN] = &page_main,
    [UI_STATE_MESSAGE_LIST] = &page_list,
    [UI_STATE_MESSAGE_READ] = &page_message,
    [UI_STATE_SETTINGS] = &page_settings,
};

/* ================== 脏标记与回调 ================== */
static bool s_needs_redraw = true;
static void (*s_redraw_cb)(void) = NULL;

void ui_request_redraw(void) {
    s_needs_redraw = true;
    if (s_redraw_cb) {
        s_redraw_cb();
    }
}

void ui_set_redraw_callback(void (*cb)(void)) {
    s_redraw_cb = cb;
}

/* ================== 内部状态定义 ================== */
typedef struct {
    ui_state_enum_t state;
    ui_message_t messages[MAX_MESSAGES];
    int message_count;
    int current_msg_idx;
    uint32_t last_activity_time;
    bool flashlight_on;      // 手电筒状态
    uint8_t brightness;      // OLED 亮度 (10-100%)
} ui_context_t;

static ui_context_t s_ui;

/* ================== 数据访问接口 ================== */
int ui_get_message_count(void) { return s_ui.message_count; }
int ui_get_current_message_idx(void) { return s_ui.current_msg_idx; }
void ui_set_current_message_idx(int idx) {
    if (s_ui.current_msg_idx != idx) {
        s_ui.current_msg_idx = idx;
        // 优化：仅在内存中更新，不立即写 Flash
        // storage_save_messages(s_ui.messages, s_ui.message_count, s_ui.current_msg_idx);
        ui_request_redraw();
    }
}

int ui_get_unread_count(void) {
    int unread = 0;
    for(int i=0; i<s_ui.message_count; i++) {
        if(!s_ui.messages[i].is_read) unread++;
    }
    return unread;
}

ui_message_t* ui_get_message_at(int idx) {
    if (idx < 0 || idx >= s_ui.message_count) return NULL;
    return &s_ui.messages[idx];
}

/* ================== 辅助函数 ================== */
static void ui_update_activity(void) {
    s_ui.last_activity_time = board_time_ms();
}

void ui_change_page(ui_state_enum_t new_state) {
    ESP_LOGD(UI_TAG, "Changing page from %d to %d", s_ui.state, new_state);

    if (new_state == s_ui.state) {
        ESP_LOGD(UI_TAG, "Page change ignored - same state");
        return;
    }

    // Validate transitions: do not enter message-related pages when there are no messages
    if ((new_state == UI_STATE_MESSAGE_LIST || new_state == UI_STATE_MESSAGE_READ) && s_ui.message_count == 0) {
        ESP_LOGW(UI_TAG, "Attempt to enter message page but no messages exist, redirecting to MAIN");
        new_state = UI_STATE_MAIN;
    }

    // Ensure current index is within valid range when entering message pages
    if (new_state == UI_STATE_MESSAGE_LIST || new_state == UI_STATE_MESSAGE_READ) {
        if (s_ui.current_msg_idx < 0) s_ui.current_msg_idx = 0;
        if (s_ui.current_msg_idx >= s_ui.message_count && s_ui.message_count > 0) {
            // default to the most recent message for better UX
            s_ui.current_msg_idx = s_ui.message_count - 1;
        }
    }

    // 调用旧页面的 exit
    if (s_ui.state != UI_STATE_STANDBY && s_pages[s_ui.state] && s_pages[s_ui.state]->on_exit) {
        ESP_LOGD(UI_TAG, "Calling exit handler for state %d", s_ui.state);
        s_pages[s_ui.state]->on_exit();
    }

    ui_state_enum_t old_state = s_ui.state;
    s_ui.state = new_state;

    // 调用新页面的 enter
    if (new_state != UI_STATE_STANDBY && s_pages[new_state] && s_pages[new_state]->on_enter) {
        ESP_LOGD(UI_TAG, "Calling enter handler for state %d", new_state);
        s_pages[new_state]->on_enter();
    }
    
    ui_request_redraw();

    ESP_LOGD(UI_TAG, "Page change completed: %d -> %d", old_state, new_state);
}

/* ================== 核心接口实现 ================== */
void ui_init(void) {
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.brightness = DEFAULT_BRIGHTNESS;
    s_ui.flashlight_on = false;

    // 创建 UI 互斥锁（保护 UI 状态免受多任务竞态）
    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        ESP_LOGE(UI_TAG, "Failed to create UI mutex!");
    }

    // initialize NVS storage and load persisted messages
    if (storage_init() == ESP_OK) {
        int loaded_count = 0;
        int loaded_idx = 0;
        if (storage_load_messages(s_ui.messages, &loaded_count, &loaded_idx) == ESP_OK) {
            s_ui.message_count = loaded_count;
            s_ui.current_msg_idx = loaded_idx;
            ESP_LOGI(UI_TAG, "Loaded %d messages from storage, current idx=%d", loaded_count, loaded_idx);
        }
        // 加载保存的亮度设置
        uint8_t saved_brightness = 0;
        if (storage_load_brightness(&saved_brightness) == ESP_OK) {
            s_ui.brightness = saved_brightness;
            board_display_set_contrast((uint8_t)((saved_brightness * 255) / 100));
            ESP_LOGI(UI_TAG, "Loaded brightness: %d%%", saved_brightness);
        }
    } else {
        ESP_LOGW(UI_TAG, "storage_init failed");
    }
    s_ui.state = UI_STATE_MAIN;
    ui_update_activity();
    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_enter) {
        s_pages[s_ui.state]->on_enter();
    }
    /* 注册 Toast 预刷新钩子（所有帧 sendBuffer 之前自动绘制覆盖层） */
    board_display_set_pre_flush_cb(toast_pre_flush_cb);
    ui_request_redraw();
    ESP_LOGI(UI_TAG, "UI Manager initialized");
}

uint32_t ui_tick(void) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_tick: failed to acquire lock, retry soon");
        return 100;
    }

    uint32_t next_sleep_ms = 1000; // 默认最大休眠时间
    bool do_render = false;
    ui_state_enum_t render_state = s_ui.state;

    // 0. Toast 超时检查
    if (s_toast_visible && s_toast_expire_ms > 0) {
        if (board_time_ms() >= s_toast_expire_ms) {
            s_toast_visible = false;
            s_needs_redraw  = true;
        } else {
            /* toast 仍在倒计时，缩短 tick 间隔以保证及时消失 */
            uint32_t remain = s_toast_expire_ms - board_time_ms();
            if (remain < next_sleep_ms) next_sleep_ms = remain;
        }
    }

    // 1. 逻辑更新 (持有锁)
    if (s_ui.state != UI_STATE_STANDBY) {
        // 检查自动待机超时
        if (board_time_ms() - s_ui.last_activity_time > STANDBY_TIMEOUT_MS) {
            ESP_LOGD(UI_TAG, "Activity timeout, entering standby");
            ui_enter_standby(); // 状态变为 STANDBY
            render_state = UI_STATE_STANDBY;
            s_needs_redraw = true;
            next_sleep_ms = 33; // 待机动画刷新率 30fps（提升流畅性）
        } else {
            // 调用当前页面的 update 逻辑
            if (s_pages[s_ui.state] && s_pages[s_ui.state]->update) {
                uint32_t page_sleep = s_pages[s_ui.state]->update();
                if (page_sleep < next_sleep_ms) next_sleep_ms = page_sleep;
            }
        }
    } else {
        // 待机状态逻辑
        next_sleep_ms = 33; // 动画刷新率 30fps（提升流畅性）
        s_needs_redraw = true; // 待机状态始终重绘动画
    }

    // 2. 决定是否渲染
    if (s_needs_redraw) {
        do_render = true;
        s_needs_redraw = false; // 清除标志
    }

    // 3. 释放锁 (关键优化：渲染过程不持有锁，避免阻塞按键中断)
    ui_unlock();

    // 4. 执行渲染 (无锁状态)
    if (do_render) {
        if (render_state == UI_STATE_STANDBY) {
            ui_render_standby();
        } else if (s_pages[render_state] && s_pages[render_state]->render) {
            s_pages[render_state]->render();
        }
    }

    return next_sleep_ms > 0 ? next_sleep_ms : 1000;
}

void ui_on_key(board_key_t key) {
    ESP_LOGI(UI_TAG, "UI received key: %d, current state: %d", key, s_ui.state);

    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_on_key: failed to acquire lock, drop key %d", key);
        return;
    }

    ui_update_activity();

    /* Toast 拦截：任意按键立即关闭 toast，不传递给页面 */
    if (s_toast_visible) {
        s_toast_visible = false;
        s_needs_redraw  = true;
        ui_unlock();
        return;
    }

    if (s_ui.state == UI_STATE_STANDBY) {
        ESP_LOGI(UI_TAG, "Waking up from standby with key %d", key);
        ui_wake_up();
        // 唤醒后切换回主界面
        ui_change_page(UI_STATE_MAIN);
        // 当前按键不继续处理，用户需要重新按键来控制主界面
        ui_unlock();
        return;
    }

    if (s_pages[s_ui.state] && s_pages[s_ui.state]->on_key) {
        ESP_LOGI(UI_TAG, "Passing key %d to page handler for state %d", key, s_ui.state);
        s_pages[s_ui.state]->on_key(key);
        // 假设按键处理会导致 UI 变化，请求重绘
        ui_request_redraw();
    } else {
        ESP_LOGW(UI_TAG, "No key handler for state %d", s_ui.state);
    }

    ui_unlock();
}

void ui_show_message(const char* sender, const char* text) {
    // 调用带时间戳的版本，使用当前时间
    ui_show_message_with_timestamp(sender, text, (uint32_t)time(NULL));
}

void ui_show_message_with_timestamp(const char* sender, const char* text, uint32_t timestamp) {
    if (!ui_lock()) {
        ESP_LOGW(UI_TAG, "ui_show_message_with_timestamp: failed to acquire lock, message dropped");
        return;
    }

    if (s_ui.message_count >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            s_ui.messages[i] = s_ui.messages[i + 1];
        }
        s_ui.message_count = MAX_MESSAGES - 1;
    }

    ui_message_t* msg = &s_ui.messages[s_ui.message_count++];
    strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    msg->sender[sizeof(msg->sender)-1] = '\0';
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text)-1] = '\0';
    msg->timestamp = timestamp;
    msg->is_read = false;

    ESP_LOGI(UI_TAG, "显示消息 - 发送者: %s, 时间戳: %u", sender, timestamp);

    ui_wake_up();
    s_ui.current_msg_idx = s_ui.message_count - 1;
    /* ui_change_page 会调用 ui_request_redraw */
    ui_change_page(UI_STATE_MESSAGE_READ);
    
    /* Toast 通知已禁用 - 收到消息时直接跳转到消息页面，无需额外提示 */
    // char toast_msg[128];
    // snprintf(toast_msg, sizeof(toast_msg), "新消息来自 %s", sender);
    // ui_show_toast(toast_msg, 3000);

    /* 快照消息数组以供锁外持久化
     * NVS 写入约 10-50ms，在锁内执行会导致：
     *   - ui_tick 等待锁超时（200ms 门限），跳过帧渲染
     *   - ui_on_key 在按键敲击时无法及时响应
     * 因此必须在释放锁后执行。
     */
    ui_message_t snap[MAX_MESSAGES];
    int snap_count = s_ui.message_count;
    int snap_idx   = s_ui.current_msg_idx;
    memcpy(snap, s_ui.messages, sizeof(ui_message_t) * (size_t)snap_count);

    ui_unlock(); /* ─── 释放锁，以下均在无锁状态执行 ─── */

    /* NVS 持久化（慢速写入，已在锁外，不阻塞 ui_tick / ui_on_key） */
    storage_save_messages(snap, snap_count, snap_idx);

    /* 硬件通知（在 app_task 上下文中，安全调用） */
    board_notify();
    board_leds_double_flash();
    board_vibrate_double();
}

void ui_enter_standby(void) {
    if (s_ui.state != UI_STATE_STANDBY) {
        // 使用统一的页面切换流程以触发当前页面的 exit handler
        ui_change_page(UI_STATE_STANDBY);
        // 恢复亮度并重置待机计时器
        board_display_set_contrast((uint8_t)((s_ui.brightness * 255) / 100));
        // 重置待机计时器标志（由 ui_render_standby 使用）
        ui_render_standby_reset_timer();
        // 渲染待机屏保
        ui_render_standby();
        // 进入待机时，只有手电筒未开启才关闭 LED
        if (!s_ui.flashlight_on) {
            board_leds_off();
        }
        ESP_LOGI(UI_TAG, "Entered standby");
    }
}

bool ui_is_in_standby(void) {
    return s_ui.state == UI_STATE_STANDBY;
}

void ui_wake_up(void) {
    if (s_ui.state == UI_STATE_STANDBY) {
        // 强制清屏并恢复显示驱动初始状态
        // 这是必要的，因为屏保黑屏状态可能导致显示驱动出现坐标系偏移
        board_display_begin();
        board_display_set_draw_color(0);
        board_display_rect(0, 0, 128, 64, true);  // 清空缓冲区
        board_display_end();
        
        ui_change_page(UI_STATE_MAIN);
        ui_update_activity();
        // 恢复显示驱动状态（屏保可能修改了这些状态）
        board_display_set_contrast((uint8_t)((s_ui.brightness * 255) / 100));
        board_display_set_draw_color(1);  // 恢复绘制颜色为白
        // 重置待机计时器，以便下次进入待机时重新计时
        ui_render_standby_reset_timer();
        ESP_LOGI(UI_TAG, "Woke up from standby, display cleared and state restored, timer reset");
    }
}

/* ================== 消息删除功能 ================== */
void ui_delete_current_message(void) {
    /* 由 on_key -> ui_on_key 调用，此时 UI 互斥锁已被持有。
     * 不得在此调用 storage_save_messages（NVS 写入 ~10-50ms 会阻塞 GUI 任务）。
     * 改为：快照数据 + 设标志，由 app_loop 调用 ui_flush_pending_saves() 完成写入。 */
    if (s_ui.message_count <= 0) return;

    int idx = s_ui.current_msg_idx;
    if (idx < 0 || idx >= s_ui.message_count) return;

    // 移动后面的消息
    for (int i = idx; i < s_ui.message_count - 1; i++) {
        s_ui.messages[i] = s_ui.messages[i + 1];
    }
    s_ui.message_count--;

    // 调整当前索引
    if (s_ui.current_msg_idx >= s_ui.message_count && s_ui.message_count > 0) {
        s_ui.current_msg_idx = s_ui.message_count - 1;
    }

    // 快照以供锁外持久化
    memcpy(s_save_snap, s_ui.messages, sizeof(storage_message_t) * (size_t)s_ui.message_count);
    s_save_count = s_ui.message_count;
    s_save_idx   = s_ui.current_msg_idx;
    s_deferred_msg_save = true;

    ui_request_redraw();

    ESP_LOGI(UI_TAG, "Deleted message at idx %d, remaining: %d", idx, s_ui.message_count);
}

/* ================== 手电筒功能 ================== */
bool ui_is_flashlight_on(void) {
    return s_ui.flashlight_on;
}

void ui_toggle_flashlight(void) {
    s_ui.flashlight_on = !s_ui.flashlight_on;

    if (s_ui.flashlight_on) {
        // 点亮所有 LED 作为手电筒
        board_leds_t leds = { .led1 = 255, .led2 = 255, .led3 = 255 };
        board_leds_set(leds);
        ESP_LOGI(UI_TAG, "Flashlight ON");
    } else {
        ESP_LOGI(UI_TAG, "Flashlight OFF");
        board_leds_off(); // 确保关闭
    }
    ui_request_redraw();
}

/* ================== 亮度控制 ================== */
uint8_t ui_get_brightness(void) {
    return s_ui.brightness;
}

void ui_set_brightness(uint8_t level) {
    if (level < 10) level = 10;
    if (level > 100) level = 100;
    s_ui.brightness = level;

    // 立即应用亮度到显示器（纯内存操作，不涉及 NVS）
    board_display_set_contrast((uint8_t)((level * 255) / 100));

    // 延迟保存：此函数在 ui_on_key 持锁时被调用，
    // 直接写 NVS 会阻塞 GUI 任务。由 ui_flush_pending_saves() 在锁外执行。
    s_save_brightness = level;
    s_deferred_brightness_save = true;

    ui_request_redraw();

    ESP_LOGI(UI_TAG, "Brightness set to %d%%", level);
}

/* ================== 系统控制 ================== */
void ui_system_restart(void) {
    ESP_LOGI(UI_TAG, "System restart requested from UI");
    // 关闭显示以给用户重启反馈
    board_display_set_contrast(0);
    board_execute_cleanup();
    board_system_restart();
}

/* ================== 延迟 NVS 持久化 ================== */
void ui_flush_pending_saves(void) {
    /* 在 app_task（无锁）上下文调用，执行之前因持锁而推迟的 NVS 写入。
     * 每次写入约 10-50ms，调用前确认不持有 UI 互斥锁。 */
    if (s_deferred_msg_save) {
        s_deferred_msg_save = false;
        storage_save_messages(s_save_snap, s_save_count, s_save_idx);
        ESP_LOGD(UI_TAG, "Deferred message save completed (%d msgs)", s_save_count);
    }
    if (s_deferred_brightness_save) {
        s_deferred_brightness_save = false;
        storage_save_brightness(s_save_brightness);
        ESP_LOGD(UI_TAG, "Deferred brightness save completed (%d%%)", s_save_brightness);
    }
}

/* ================== Toast API ================== */

void ui_show_toast(const char *msg, uint32_t auto_dismiss_ms) {
    if (!msg) return;
    strncpy(s_toast_msg, msg, TOAST_MSG_MAX - 1);
    s_toast_msg[TOAST_MSG_MAX - 1] = '\0';
    s_toast_visible   = true;
    s_toast_expire_ms = (auto_dismiss_ms > 0) ? (board_time_ms() + auto_dismiss_ms) : 0;
    ui_request_redraw();
    ESP_LOGD(UI_TAG, "Toast shown: \"%s\" (auto_dismiss=%ums)", s_toast_msg, auto_dismiss_ms);
}

bool ui_toast_is_visible(void) {
    return s_toast_visible;
}

void ui_toast_dismiss(void) {
    if (s_toast_visible) {
        s_toast_visible = false;
        ui_request_redraw();
    }
}
