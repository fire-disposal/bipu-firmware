#include "board_pins.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * LED 状态机 v2 — 双层优先级架构
 *
 *  优先级（从高到低）：
 *    FLASHLIGHT  → bool 标志，三灯全亮，完全覆盖其他层
 *    FOREGROUND  → 一次性效果（短闪/双闪/通知），完成后自动退回背景
 *    BACKGROUND  → 持久背景（广播跑马/慢闪/常灭）
 *
 *  设计特点：
 *    - 描述符驱动：新增效果只需定义一个 led_anim_desc_t 常量
 *    - 前景结束后自动续接背景，无需外部干预
 *    - 单一互斥锁保护所有状态，thread-safe（NimBLE 任务可安全调用）
 * ========================================================= */

/* ── 效果描述符 ─────────────────────────────────────────── */

typedef enum {
    LED_ANIM_OFF,       /* 全灭（静态，layer_start 写 GPIO 后无需 tick） */
    LED_ANIM_STATIC,    /* 全亮（静态，同上） */
    LED_ANIM_BLINK,     /* on_ms 亮 → off_ms 灭，循环 count 次（0=永久） */
    LED_ANIM_MARQUEE,   /* 三灯依次轮亮，on_ms 作为步进间隔 */
} led_anim_type_t;

typedef struct {
    led_anim_type_t type;
    uint32_t on_ms;   /* BLINK: 亮持续时间；MARQUEE: 步进间隔 */
    uint32_t off_ms;  /* BLINK: 灭持续时间 */
    uint8_t  count;   /* BLINK: 重复次数，0=永久 */
} led_anim_desc_t;

/* ── 预设效果常量 ─────────────────────────────────────────── */

static const led_anim_desc_t ANIM_OFF           = {LED_ANIM_OFF,     0,   0,   0};
static const led_anim_desc_t ANIM_STATIC        = {LED_ANIM_STATIC,  0,   0,   0};
static const led_anim_desc_t ANIM_MARQUEE_300   = {LED_ANIM_MARQUEE, 300, 0,   0};
static const led_anim_desc_t ANIM_BLINK_SLOW    = {LED_ANIM_BLINK,   500, 500, 0};
static const led_anim_desc_t ANIM_BLINK_2_FAST  = {LED_ANIM_BLINK,   100, 100, 2};
static const led_anim_desc_t ANIM_BLINK_2_MED   = {LED_ANIM_BLINK,   200, 200, 2};

/* ── 层运行时状态 ─────────────────────────────────────────── */

typedef struct {
    led_anim_desc_t desc;    /* 当前效果参数副本 */
    bool            active;  /* FG 专用：效果是否还在运行（BG 始终为 true） */
    uint32_t        last_ms; /* 上次相位切换时间戳 */
    int             phase;   /* 当前相位（偶=亮, 奇=灭；MARQUEE=LED 索引） */
} led_layer_t;

/* ── 模块状态 ─────────────────────────────────────────────── */

static bool              s_initialized = false;
static SemaphoreHandle_t s_mutex       = NULL;
static bool              s_flashlight  = false;  /* 最高优先级覆盖 */
static led_layer_t       s_bg          = {0};    /* 背景层（持久） */
static led_layer_t       s_fg          = {0};    /* 前景层（一次性） */

/* ── GPIO 辅助 ─────────────────────────────────────────────── */

static const board_leds_t ALL_ON  = {255, 255, 255};
static const board_leds_t ALL_OFF = {0,   0,   0  };

static void gpio_write(board_leds_t leds) {
    gpio_set_level(BOARD_GPIO_LED_1, leds.led1 > 0 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_2, leds.led2 > 0 ? 1 : 0);
    gpio_set_level(BOARD_GPIO_LED_3, leds.led3 > 0 ? 1 : 0);
}

static board_leds_t marquee_leds(int idx) {
    board_leds_t l = {0, 0, 0};
    switch (idx % 3) {
        case 0: l.led1 = 255; break;
        case 1: l.led2 = 255; break;
        default: l.led3 = 255; break;
    }
    return l;
}

static inline bool leds_lock(void) {
    if (!s_mutex) return true;
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}
static inline void leds_unlock(void) {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/* ── 层启动：初始化运行时状态并输出第一帧 GPIO ─────────────── */

static void layer_start(led_layer_t *layer, led_anim_desc_t desc) {
    layer->desc    = desc;
    layer->active  = true;
    layer->last_ms = board_time_ms();
    layer->phase   = 0;
    switch (desc.type) {
        case LED_ANIM_OFF:     gpio_write(ALL_OFF);          break;
        case LED_ANIM_STATIC:  gpio_write(ALL_ON);           break;
        case LED_ANIM_BLINK:   gpio_write(ALL_ON);           break; /* 从亮相开始 */
        case LED_ANIM_MARQUEE: gpio_write(marquee_leds(0));  break;
    }
}

/* ── 层 tick：推进一步动画，返回 false 表示一次性效果已完成 ── */

static bool layer_tick(led_layer_t *layer) {
    if (!layer->active) return false;

    const led_anim_desc_t *d = &layer->desc;

    /* 静态效果无需周期切换，直接返回（GPIO 在 layer_start 中已设置） */
    if (d->type == LED_ANIM_OFF || d->type == LED_ANIM_STATIC) {
        return true;
    }

    uint32_t now = board_time_ms();

    if (d->type == LED_ANIM_BLINK) {
        /* 进入前检查是否已达到目标次数（防止 count 外部变更导致越界） */
        if (d->count > 0 && layer->phase >= (int)(2 * d->count)) {
            gpio_write(ALL_OFF);
            layer->active = false;
            return false;
        }
        uint32_t interval = (layer->phase % 2 == 0) ? d->on_ms : d->off_ms;
        if (now - layer->last_ms >= interval) {
            layer->last_ms = now;
            layer->phase++;
            if (d->count > 0 && layer->phase >= (int)(2 * d->count)) {
                gpio_write(ALL_OFF);
                layer->active = false;
                return false;
            }
            gpio_write((layer->phase % 2 == 0) ? ALL_ON : ALL_OFF);
        }
        return true;
    }

    if (d->type == LED_ANIM_MARQUEE) {
        if (now - layer->last_ms >= d->on_ms) {
            layer->last_ms = now;
            layer->phase   = (layer->phase + 1) % 3;
            gpio_write(marquee_leds(layer->phase));
        }
        return true;
    }

    return true;
}

/* =========================================================
 * 公开 API
 * ========================================================= */

void board_leds_init(void) {
    if (s_initialized) return;

    s_mutex = xSemaphoreCreateMutex();

    gpio_reset_pin(BOARD_GPIO_LED_1);
    gpio_reset_pin(BOARD_GPIO_LED_2);
    gpio_reset_pin(BOARD_GPIO_LED_3);

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_LED_1) |
                        (1ULL << BOARD_GPIO_LED_2) |
                        (1ULL << BOARD_GPIO_LED_3),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_write(ALL_OFF);

    layer_start(&s_bg, ANIM_OFF);
    s_fg.active  = false;
    s_flashlight = false;

    s_initialized = true;
    ESP_LOGI(BOARD_TAG, "LEDs initialized (dual-layer v2)");
}

/* board_leds_tick：在 app_loop 中每 10ms 调用一次 */
void board_leds_tick(void) {
    if (!s_initialized) return;
    if (!leds_lock()) return;

    if (s_flashlight) {
        gpio_write(ALL_ON);
        leds_unlock();
        return;
    }

    if (s_fg.active) {
        bool still_running = layer_tick(&s_fg);
        if (!still_running) {
            /* 前景完成 → 重播背景，恢复背景 LED 输出 */
            layer_start(&s_bg, s_bg.desc);
        }
    } else {
        layer_tick(&s_bg);
    }

    leds_unlock();
}

/* board_leds_set_mode：统一模式入口，兼容现有调用方 */
void board_leds_set_mode(board_led_mode_t mode) {
    if (!s_initialized) return;
    if (!leds_lock()) return;

    led_anim_desc_t desc;
    bool is_oneshot;  /* true=前景一次性；false=背景持久 */

    switch (mode) {
        case BOARD_LED_MODE_OFF:
            desc = ANIM_OFF;          is_oneshot = false; break;
        case BOARD_LED_MODE_STATIC:
            desc = ANIM_STATIC;       is_oneshot = false; break;
        case BOARD_LED_MODE_ADVERTISING:
        case BOARD_LED_MODE_MARQUEE:
            desc = ANIM_MARQUEE_300;  is_oneshot = false; break;
        case BOARD_LED_MODE_BLINK:
            desc = ANIM_BLINK_SLOW;   is_oneshot = false; break;
        /* 一次性效果：完成后自动回到当前背景 */
        case BOARD_LED_MODE_CONNECTED:
            desc = ANIM_BLINK_2_MED;  is_oneshot = true;  break;
        case BOARD_LED_MODE_NOTIFY_FLASH:
            desc = ANIM_BLINK_2_FAST; is_oneshot = true;  break;
        default:
            desc = ANIM_OFF; is_oneshot = false; break;
    }

    if (is_oneshot) {
        /* 前景一次性：不修改背景，覆盖在背景之上 */
        layer_start(&s_fg, desc);
    } else {
        /* 背景变更：清除前景，立即切换背景 */
        s_fg.active = false;
        layer_start(&s_bg, desc);
    }

    leds_unlock();
    ESP_LOGD(BOARD_TAG, "LED mode=%d oneshot=%d", mode, is_oneshot);
}

void board_leds_flashlight_on(void) {
    if (!s_initialized) return;
    if (!leds_lock()) return;
    s_flashlight = true;
    gpio_write(ALL_ON);
    leds_unlock();
    ESP_LOGD(BOARD_TAG, "Flashlight ON");
}

void board_leds_flashlight_off(void) {
    if (!s_initialized) return;
    if (!leds_lock()) return;
    s_flashlight = false;
    /* 恢复输出：前景仍活跃则下次 tick 处理，否则重播背景 */
    if (!s_fg.active) {
        layer_start(&s_bg, s_bg.desc);
    }
    leds_unlock();
    ESP_LOGD(BOARD_TAG, "Flashlight OFF");
}

bool board_leds_is_flashlight_on(void) { return s_flashlight; }

void board_leds_off(void) {
    if (!s_initialized) return;
    if (!leds_lock()) return;
    s_flashlight = false;
    s_fg.active  = false;
    layer_start(&s_bg, ANIM_OFF);
    leds_unlock();
}

bool board_leds_is_active(void) {
    return s_flashlight ||
           s_fg.active  ||
           (s_bg.desc.type != LED_ANIM_OFF);
}

bool board_leds_is_initialized(void) { return s_initialized; }

/* ── 遗留 API 兼容包装（签名不变，内部转发至新状态机） ─── */

void board_leds_set(board_leds_t leds) {
    if (!s_initialized) return;
    if (!leds_lock()) return;
    if (!s_flashlight) gpio_write(leds);
    leds_unlock();
}

board_leds_t board_leds_get_state(void) {
    return s_flashlight ? ALL_ON : ALL_OFF;
}

void board_leds_short_flash(void)            { board_leds_set_mode(BOARD_LED_MODE_NOTIFY_FLASH); }
void board_leds_double_flash(void)           { board_leds_set_mode(BOARD_LED_MODE_NOTIFY_FLASH); }
void board_leds_continuous_blink_start(void) { board_leds_set_mode(BOARD_LED_MODE_BLINK);        }
void board_leds_continuous_blink_stop(void)  { board_leds_set_mode(BOARD_LED_MODE_OFF);          }
void board_leds_gallop_start(void)           { board_leds_set_mode(BOARD_LED_MODE_MARQUEE);      }
void board_leds_gallop_stop(void)            { board_leds_set_mode(BOARD_LED_MODE_OFF);          }
void board_leds_enable_ble_auto_indicator(bool enable) { (void)enable; }
void board_leds_set_ble_state_callbacks(
    void (*cc)(bool connected), void (*ac)(bool adv)) { (void)cc; (void)ac; }
