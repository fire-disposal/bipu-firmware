#include "board.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "u8g2.h"

/* ================== 私有状态 ================== */
static u8g2_t s_u8g2;
static uint32_t s_vibrate_end_time = 0;
static bool s_vibrate_active = false;

/* ================== u8g2 回调函数 ================== */
static uint8_t u8g2_esp32_i2c_byte_cb(
    u8x8_t *u8x8,
    uint8_t msg,
    uint8_t arg_int,
    void *arg_ptr)
{
    static uint8_t buffer[128];
    static uint8_t buf_idx = 0;

    switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
        buf_idx = 0;
        break;

    case U8X8_MSG_BYTE_SEND: {
        uint8_t *data = (uint8_t *)arg_ptr;
        for (int i = 0; i < arg_int && buf_idx < sizeof(buffer); i++) {
            buffer[buf_idx++] = data[i];
        }
        break;
    }

    case U8X8_MSG_BYTE_END_TRANSFER: {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(
            cmd,
            (BOARD_OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE,
            true);
        i2c_master_write(cmd, buffer, buf_idx, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(
            BOARD_I2C_MASTER_PORT,
            cmd,
            pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);

        if (ret != ESP_OK) {
            ESP_LOGE(BOARD_TAG, "I2C transfer failed: %d", ret);
            return 0;
        }
        break;
    }

    default:
        break;
    }

    return 1;
}

static uint8_t u8g2_esp32_gpio_delay_cb(
    u8x8_t *u8x8,
    uint8_t msg,
    uint8_t arg_int,
    void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;

    case U8X8_MSG_DELAY_10MICRO:
        ets_delay_us(10 * arg_int);
        break;

    default:
        break;
    }
    return 1;
}

/* ================== I2C 初始化 ================== */
static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_IO,
        .scl_io_num = BOARD_I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(BOARD_I2C_MASTER_PORT, &conf));
    ESP_ERROR_CHECK(
        i2c_driver_install(BOARD_I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0));
}

/* ================== 按键相关实现 ================== */
#define BUTTON_COUNT 4
#define DEBOUNCE_TIME_MS 50

typedef struct {
    bool is_pressed;
    uint32_t press_time;
    bool debounced;
} button_state_t;

static button_state_t s_button_states[BUTTON_COUNT];

/* 读取按键GPIO电平 */
static bool read_button_gpio(int button)
{
    gpio_num_t gpio_num;
    switch (button) {
        case 0: gpio_num = BOARD_GPIO_KEY_UP; break;
        case 1: gpio_num = BOARD_GPIO_KEY_DOWN; break;
        case 2: gpio_num = BOARD_GPIO_KEY_ENTER; break;
        case 3: gpio_num = BOARD_GPIO_KEY_BACK; break;
        default: return false;
    }
    // 按键按下时接GND，所以读取到0表示按下
    return (gpio_get_level(gpio_num) == 0);
}

/* 按键去抖动处理 */
static bool button_debounce(int button)
{
    button_state_t* state = &s_button_states[button];
    uint32_t current_time = board_time_ms();
    bool raw_state = read_button_gpio(button);

    if (raw_state != state->is_pressed) {
        state->is_pressed = raw_state;
        state->press_time = current_time;
        state->debounced = false;
        return raw_state; // 上升沿触发
    }
    
    if (!state->debounced && (current_time - state->press_time) >= DEBOUNCE_TIME_MS) {
        state->debounced = true;
        return state->is_pressed;
    }
    
    return false;
}

/* ================== GPIO初始化 ================== */
static void gpio_init(void)
{
    // 配置按键GPIO为输入，上拉模式（按钮按下时接GND）
    gpio_config_t key_config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_KEY_UP) |
                       (1ULL << BOARD_GPIO_KEY_DOWN) |
                       (1ULL << BOARD_GPIO_KEY_ENTER) |
                       (1ULL << BOARD_GPIO_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&key_config);
    
    // 配置震动马达GPIO为输出
    gpio_config_t vibrate_config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_VIBRATE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vibrate_config);
    
    // 配置RGB灯GPIO为输出
    gpio_config_t rgb_config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_RGB_R) |
                       (1ULL << BOARD_GPIO_RGB_G) |
                       (1ULL << BOARD_GPIO_RGB_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rgb_config);
    
    // 初始状态：震动关闭，RGB灯关闭
    gpio_set_level(BOARD_GPIO_VIBRATE, 0);
    gpio_set_level(BOARD_GPIO_RGB_R, 0);
    gpio_set_level(BOARD_GPIO_RGB_G, 0);
    gpio_set_level(BOARD_GPIO_RGB_B, 0);
}

/* ================== 板级初始化 ================== */
void board_init(void)
{
    ESP_LOGI(BOARD_TAG, "Initializing board...");
    
    // GPIO初始化
    gpio_init();
    
    // I2C初始化
    i2c_master_init();
    
    // OLED显示屏初始化
    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(
        &s_u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_delay_cb);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SendBuffer(&s_u8g2);
    
    // 按键初始化
    for (int i = 0; i < BUTTON_COUNT; i++) {
        s_button_states[i].is_pressed = false;
        s_button_states[i].press_time = 0;
        s_button_states[i].debounced = false;
    }
    
    ESP_LOGI(BOARD_TAG, "Board initialized");
}

/* ================== 显示接口实现 ================== */
void board_display_begin(void)
{
    u8g2_ClearBuffer(&s_u8g2);
}

void board_display_end(void)
{
    u8g2_SendBuffer(&s_u8g2);
}

void board_display_text(int x, int y, const char* text)
{
    if (text) {
        u8g2_SetFont(&s_u8g2, u8g2_font_wqy12_t_chinese3);
        u8g2_DrawUTF8(&s_u8g2, x, y, text);
    }
}

void board_display_rect(int x, int y, int w, int h, bool fill)
{
    if (fill) {
        u8g2_DrawBox(&s_u8g2, x, y, w, h);
    } else {
        u8g2_DrawFrame(&s_u8g2, x, y, w, h);
    }
}

/* ================== 输入接口实现 ================== */
board_key_t board_key_poll(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (button_debounce(i)) {
            switch (i) {
                case 0: return BOARD_KEY_UP;
                case 1: return BOARD_KEY_DOWN;
                case 2: return BOARD_KEY_ENTER;
                case 3: return BOARD_KEY_BACK;
                default: break;
            }
        }
    }
    return BOARD_KEY_NONE;
}

/* ================== 时间接口实现 ================== */
uint32_t board_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void board_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ================== 震动接口实现 ================== */
void board_vibrate_init(void)
{
    // 震动马达GPIO已经在gpio_init()中初始化
    gpio_set_level(BOARD_GPIO_VIBRATE, 0);
    s_vibrate_active = false;
    s_vibrate_end_time = 0;
}

void board_vibrate_on(uint32_t ms)
{
    gpio_set_level(BOARD_GPIO_VIBRATE, 1);
    s_vibrate_active = true;
    if (ms > 0) {
        s_vibrate_end_time = board_time_ms() + ms;
    } else {
        s_vibrate_end_time = 0; // 持续震动，需要手动关闭
    }
}

void board_vibrate_off(void)
{
    gpio_set_level(BOARD_GPIO_VIBRATE, 0);
    s_vibrate_active = false;
    s_vibrate_end_time = 0;
}

/* ================== 震动状态管理 ================== */
static void board_vibrate_tick(void)
{
    if (s_vibrate_active && s_vibrate_end_time > 0) {
        uint32_t current_time = board_time_ms();
        if (current_time >= s_vibrate_end_time) {
            board_vibrate_off();
        }
    }
}

/* ================== RGB灯接口实现 ================== */
void board_rgb_init(void)
{
    // RGB灯GPIO已经在gpio_init()中初始化
    board_rgb_off();
}

void board_rgb_set_color(board_rgb_color_t color)
{
    switch (color) {
        case BOARD_RGB_OFF:
            gpio_set_level(BOARD_GPIO_RGB_R, 0);
            gpio_set_level(BOARD_GPIO_RGB_G, 0);
            gpio_set_level(BOARD_GPIO_RGB_B, 0);
            break;
        case BOARD_RGB_RED:
            gpio_set_level(BOARD_GPIO_RGB_R, 1);
            gpio_set_level(BOARD_GPIO_RGB_G, 0);
            gpio_set_level(BOARD_GPIO_RGB_B, 0);
            break;
        case BOARD_RGB_GREEN:
            gpio_set_level(BOARD_GPIO_RGB_R, 0);
            gpio_set_level(BOARD_GPIO_RGB_G, 1);
            gpio_set_level(BOARD_GPIO_RGB_B, 0);
            break;
        case BOARD_RGB_BLUE:
            gpio_set_level(BOARD_GPIO_RGB_R, 0);
            gpio_set_level(BOARD_GPIO_RGB_G, 0);
            gpio_set_level(BOARD_GPIO_RGB_B, 1);
            break;
        case BOARD_RGB_YELLOW:
            gpio_set_level(BOARD_GPIO_RGB_R, 1);
            gpio_set_level(BOARD_GPIO_RGB_G, 1);
            gpio_set_level(BOARD_GPIO_RGB_B, 0);
            break;
        case BOARD_RGB_CYAN:
            gpio_set_level(BOARD_GPIO_RGB_R, 0);
            gpio_set_level(BOARD_GPIO_RGB_G, 1);
            gpio_set_level(BOARD_GPIO_RGB_B, 1);
            break;
        case BOARD_RGB_MAGENTA:
            gpio_set_level(BOARD_GPIO_RGB_R, 1);
            gpio_set_level(BOARD_GPIO_RGB_G, 0);
            gpio_set_level(BOARD_GPIO_RGB_B, 1);
            break;
        case BOARD_RGB_WHITE:
            gpio_set_level(BOARD_GPIO_RGB_R, 1);
            gpio_set_level(BOARD_GPIO_RGB_G, 1);
            gpio_set_level(BOARD_GPIO_RGB_B, 1);
            break;
    }
}

void board_rgb_off(void)
{
    gpio_set_level(BOARD_GPIO_RGB_R, 0);
    gpio_set_level(BOARD_GPIO_RGB_G, 0);
    gpio_set_level(BOARD_GPIO_RGB_B, 0);
}

/* ================== 反馈接口实现 ================== */
void board_notify(void)
{
    // 震动提醒 + RGB灯闪烁（非阻塞方式）
    board_vibrate_on(100); // 震动100ms
    board_rgb_set_color(BOARD_RGB_BLUE);
    // RGB灯状态由调用者管理，这里不自动关闭
    ESP_LOGI(BOARD_TAG, "Notification triggered");
}


/* ================== BLE接口实现 ================== */
void board_ble_init(void)
{
    // TODO: 实现BLE初始化
    ESP_LOGI(BOARD_TAG, "BLE init placeholder");
}

void board_ble_poll(void)
{
    // TODO: 实现BLE消息轮询
}