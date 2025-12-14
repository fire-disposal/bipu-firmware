#include "ui_input.h"
#include "astra_ui_core.h"
#include "astra_ui_item.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ui_input";

// 按键 GPIO 定义
#define KEY_UP_GPIO     GPIO_NUM_10
#define KEY_DOWN_GPIO   GPIO_NUM_11
#define KEY_ENTER_GPIO  GPIO_NUM_12
#define KEY_BACK_GPIO   GPIO_NUM_13

// 使用消息队列传递按键事件，避免 ISR 内直接处理
static QueueHandle_t key_event_queue = NULL;

typedef enum {
    KEY_EVENT_UP,
    KEY_EVENT_DOWN,
    KEY_EVENT_ENTER,
    KEY_EVENT_BACK
} key_event_t;

// 按键事件处理（ISR 内仅发送队列）
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    key_event_t event;

    switch (gpio_num) {
        case KEY_UP_GPIO:    event = KEY_EVENT_UP;    break;
        case KEY_DOWN_GPIO:  event = KEY_EVENT_DOWN;  break;
        case KEY_ENTER_GPIO: event = KEY_EVENT_ENTER; break;
        case KEY_BACK_GPIO:  event = KEY_EVENT_BACK;  break;
        default: return;
    }

    // 非阻塞发送，避免 ISR 内阻塞
    xQueueSendFromISR(key_event_queue, &event, NULL);
}

// 初始化按键 GPIO（上拉输入，上升沿触发）
static void ui_input_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) | (1ULL << KEY_ENTER_GPIO) | (1ULL << KEY_BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io_conf);

    // 安装 GPIO 中断服务
    gpio_install_isr_service(0);

    // 注册中断处理函数
    gpio_isr_handler_add(KEY_UP_GPIO, gpio_isr_handler, (void*)KEY_UP_GPIO);
    gpio_isr_handler_add(KEY_DOWN_GPIO, gpio_isr_handler, (void*)KEY_DOWN_GPIO);
    gpio_isr_handler_add(KEY_ENTER_GPIO, gpio_isr_handler, (void*)KEY_ENTER_GPIO);
    gpio_isr_handler_add(KEY_BACK_GPIO, gpio_isr_handler, (void*)KEY_BACK_GPIO);
}

// 按键事件处理任务（阻塞读取队列）
static void ui_input_event_task(void* arg)
{
    key_event_t event;
    while (1) {
        if (xQueueReceive(key_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            switch (event) {
                case KEY_EVENT_UP:    ui_input_on_key_up();    break;
                case KEY_EVENT_DOWN:  ui_input_on_key_down();  break;
                case KEY_EVENT_ENTER: ui_input_on_key_enter(); break;
                case KEY_EVENT_BACK:  ui_input_on_key_back();  break;
            }
        }
    }
}

// 对外初始化接口
void ui_input_init(void)
{
    // 创建消息队列
    key_event_queue = xQueueCreate(16, sizeof(key_event_t));
    if (key_event_queue == NULL) {
        ESP_LOGE(TAG, "按键事件队列创建失败");
        return;
    }

    ui_input_gpio_init();

    // 创建按键事件处理任务，优先级高于 UI 渲染
    xTaskCreate(ui_input_event_task, "ui_input_event", 2048, NULL, 6, NULL);

    ESP_LOGI(TAG, "按键输入初始化完成：GPIO 10/11/12/13，上拉输入，上升沿触发，使用消息队列");
}

// 按键事件处理
void ui_input_on_key_down(void)
{
    ESP_LOGI(TAG, "Key Down -> 下一项");
    astra_selector_go_next_item();
}

void ui_input_on_key_up(void)
{
    ESP_LOGI(TAG, "Key Up -> 上一项");
    astra_selector_go_prev_item();
}

void ui_input_on_key_enter(void)
{
    ESP_LOGI(TAG, "Key Enter -> 进入/确认");
    astra_selector_jump_to_selected_item();
}

void ui_input_on_key_back(void)
{
    ESP_LOGI(TAG, "Key Back -> 返回");
    astra_selector_exit_current_item();
}