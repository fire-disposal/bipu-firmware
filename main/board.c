#include "board.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "u8g2.h"

// BLE相关头文件
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "nvs_flash.h"

/* ================== 私有状态 ================== */
static u8g2_t s_u8g2;
static uint32_t s_vibrate_end_time = 0;
static bool s_vibrate_active = false;

/* ================== BLE私有状态 ================== */
static board_ble_state_t s_ble_state = BOARD_BLE_STATE_UNINITIALIZED;
static bool s_ble_connected = false;
static board_ble_message_cb_t s_ble_message_callback = NULL;
static uint8_t s_ble_service_handle = 0;
static uint8_t s_ble_char_handle = 0;
static uint16_t s_ble_conn_id = 0xFFFF;
static uint32_t s_ble_error_count = 0;
static uint32_t s_ble_last_error_time = 0;

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
    
    // 初始化NVS（用于BLE配置存储）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
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
void board_vibrate_tick(void)
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


/* ================== BLE GATT服务器配置 ================== */
// 使用标准的128位UUID格式
static const uint8_t s_ble_service_uuid[16] = {
    0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t s_ble_char_uuid[16] = {
    0x78, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 使用ESP-IDF推荐的UUID格式
static const uint8_t s_ble_service_uuid_128[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // 自定义服务UUID: 00001234-0000-0000-0000-000000000000
    0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t s_ble_char_uuid_128[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // 自定义特征UUID: 00005678-0000-0000-0000-000000000000
    0x78, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t s_ble_char_value[1] = {0x00};

static esp_attr_value_t s_ble_char_attr_value = {
    .attr_max_len = BOARD_BLE_MAX_MESSAGE_LEN,
    .attr_len     = sizeof(s_ble_char_value),
    .attr_value   = (uint8_t*)s_ble_char_value,
};

/* ================== BLE错误处理 ================== */
static void ble_handle_error(const char* operation, esp_err_t error)
{
    s_ble_error_count++;
    s_ble_last_error_time = board_time_ms();
    s_ble_state = BOARD_BLE_STATE_ERROR;
    
    ESP_LOGE(BOARD_TAG, "BLE错误 - 操作: %s, 错误码: %s (0x%x)",
             operation, esp_err_to_name(error), error);
    
    // 错误指示：红色LED闪烁
    board_rgb_set_color(BOARD_RGB_RED);
}

/* ================== BLE状态字符串转换 ================== */
const char* board_ble_state_to_string(board_ble_state_t state)
{
    switch (state) {
        case BOARD_BLE_STATE_UNINITIALIZED: return "未初始化";
        case BOARD_BLE_STATE_INITIALIZING:  return "初始化中";
        case BOARD_BLE_STATE_INITIALIZED:   return "已初始化";
        case BOARD_BLE_STATE_ADVERTISING:   return "广播中";
        case BOARD_BLE_STATE_CONNECTED:     return "已连接";
        case BOARD_BLE_STATE_ERROR:         return "错误状态";
        default: return "未知状态";
    }
}

/* ================== BLE事件处理 ================== */
static void ble_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(BOARD_TAG, "GATT注册事件, app_id %d, status %d", param->reg.app_id, param->reg.status);
            if (param->reg.status == ESP_GATT_OK) {
                s_ble_service_handle = gatts_if;
                s_ble_state = BOARD_BLE_STATE_INITIALIZED;
                esp_gatt_srvc_id_t service_id = {
                    .id = {
                        .uuid = {
                            .len = ESP_UUID_LEN_128,
                            .uuid = {0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
                        },
                        .inst_id = 0
                    },
                    .is_primary = true
                };
                esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 4);
                if (ret != ESP_OK) {
                    ble_handle_error("创建GATT服务", ret);
                }
            } else {
                ble_handle_error("GATT注册", ESP_FAIL);
            }
            break;
            
        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(BOARD_TAG, "GATT服务创建事件, status %d", param->create.status);
            if (param->create.status == ESP_GATT_OK) {
                s_ble_service_handle = param->create.service_handle;
                s_ble_state = BOARD_BLE_STATE_ADVERTISING;
                esp_err_t ret = esp_ble_gatts_start_service(param->create.service_handle);
                if (ret != ESP_OK) {
                    ble_handle_error("启动GATT服务", ret);
                    break;
                }
                
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
                rsp.attr_value.handle = param->create.service_handle;
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = 0x00;
                
                esp_bt_uuid_t char_uuid = {
                    .len = ESP_UUID_LEN_128,
                    .uuid = {.uuid128 = {0x78, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}
                };
                ret = esp_ble_gatts_add_char(param->create.service_handle,
                                     &char_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                     &s_ble_char_attr_value, NULL);
                if (ret != ESP_OK) {
                    ble_handle_error("添加GATT特征", ret);
                }
            } else {
                ble_handle_error("创建GATT服务", ESP_FAIL);
            }
            break;
            
        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(BOARD_TAG, "GATT特征添加事件, status %d, attr_handle %d",
                    param->add_char.status, param->add_char.attr_handle);
            if (param->add_char.status == ESP_GATT_OK) {
                s_ble_char_handle = param->add_char.attr_handle;
            } else {
                ble_handle_error("添加GATT特征", ESP_FAIL);
            }
            break;
            
        case ESP_GATTS_START_EVT:
            ESP_LOGI(BOARD_TAG, "GATT服务启动事件, status %d", param->start.status);
            if (param->start.status == ESP_GATT_OK) {
                ESP_LOGI(BOARD_TAG, "BLE服务已启动，开始广播...");
            } else {
                ble_handle_error("启动GATT服务", ESP_FAIL);
            }
            break;
            
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(BOARD_TAG, "BLE连接事件, conn_id %d", param->connect.conn_id);
            s_ble_connected = true;
            s_ble_conn_id = param->connect.conn_id;
            s_ble_state = BOARD_BLE_STATE_CONNECTED;
            board_notify(); // 连接成功提醒
            ESP_LOGI(BOARD_TAG, "BLE已连接，设备名称: %s", BOARD_BLE_DEVICE_NAME);
            break;
            
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(BOARD_TAG, "BLE断开连接事件, conn_id %d", param->disconnect.conn_id);
            s_ble_connected = false;
            s_ble_conn_id = 0xFFFF;
            s_ble_state = BOARD_BLE_STATE_ADVERTISING;
            board_rgb_off(); // 断开连接时关闭RGB灯
            ESP_LOGI(BOARD_TAG, "BLE已断开，重新广播中...");
            break;
            
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(BOARD_TAG, "GATT写事件, conn_id %d, handle %d, len %d",
                    param->write.conn_id, param->write.handle, param->write.len);
            
            if (param->write.handle == s_ble_char_handle && param->write.len > 0) {
                // 解析接收到的消息格式：sender|message
                char received_data[BOARD_BLE_MAX_MESSAGE_LEN + 1] = {0};
                memcpy(received_data, param->write.value, param->write.len);
                received_data[param->write.len] = '\0';
                
                ESP_LOGI(BOARD_TAG, "接收到BLE消息: %s", received_data);
                
                // 解析消息格式
                char* separator = strchr(received_data, '|');
                if (separator != NULL) {
                    *separator = '\0';
                    char* sender = received_data;
                    char* message = separator + 1;
                    
                    // 调用消息回调函数
                    if (s_ble_message_callback != NULL) {
                        s_ble_message_callback(sender, message);
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

/* ================== BLE接口实现 ================== */
void board_ble_init(void)
{
    if (s_ble_state != BOARD_BLE_STATE_UNINITIALIZED) {
        ESP_LOGW(BOARD_TAG, "BLE已经初始化，当前状态: %s", board_ble_state_to_string(s_ble_state));
        return;
    }
    
    ESP_LOGI(BOARD_TAG, "初始化BLE...");
    s_ble_state = BOARD_BLE_STATE_INITIALIZING;
    
    esp_err_t ret;
    
    // 1. 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器初始化", ret);
        return;
    }
    
    // 2. 使能蓝牙控制器
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙控制器使能", ret);
        return;
    }
    
    // 3. 初始化蓝牙协议栈
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈初始化", ret);
        return;
    }
    
    // 4. 使能蓝牙协议栈
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ble_handle_error("蓝牙协议栈使能", ret);
        return;
    }
    
    // 5. 注册GATT回调函数
    ret = esp_ble_gatts_register_callback(ble_event_handler);
    if (ret != ESP_OK) {
        ble_handle_error("GATT回调注册", ret);
        return;
    }
    
    // 6. 注册GATT应用
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ble_handle_error("GATT应用注册", ret);
        return;
    }
    
    // 7. 设置设备名称
    ret = esp_ble_gap_set_device_name(BOARD_BLE_DEVICE_NAME);
    if (ret != ESP_OK) {
        ble_handle_error("设置设备名称", ret);
        return;
    }
    
    // 8. 配置广播数据
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x20,
        .max_interval = 0x40,
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = ESP_UUID_LEN_128,
        .p_service_uuid = (uint8_t*)s_ble_service_uuid_128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret != ESP_OK) {
        ble_handle_error("配置广播数据", ret);
        return;
    }
    
    ESP_LOGI(BOARD_TAG, "BLE初始化完成，等待服务创建...");
}

void board_ble_poll(void)
{
    // BLE事件通过回调函数处理，这里不需要轮询
    // 可以在这里添加状态检查或心跳功能
    if (s_ble_connected) {
        // 可以添加连接状态指示，比如RGB灯闪烁
        static uint32_t last_blink_time = 0;
        uint32_t current_time = board_time_ms();
        
        if (current_time - last_blink_time > 1000) { // 每秒闪烁一次
            static bool led_state = false;
            if (led_state) {
                board_rgb_set_color(BOARD_RGB_BLUE);
            } else {
                board_rgb_off();
            }
            led_state = !led_state;
            last_blink_time = current_time;
        }
    }
}

void board_ble_set_message_callback(board_ble_message_cb_t callback)
{
    s_ble_message_callback = callback;
}

bool board_ble_is_connected(void)
{
    return s_ble_connected;
}

const char* board_ble_get_device_name(void)
{
    return BOARD_BLE_DEVICE_NAME;
}

board_ble_state_t board_ble_get_state(void)
{
    return s_ble_state;
}

void board_ble_start_advertising(void)
{
    if (s_ble_state == BOARD_BLE_STATE_INITIALIZED || s_ble_state == BOARD_BLE_STATE_ADVERTISING) {
        ESP_LOGI(BOARD_TAG, "开始BLE广播...");
        
        esp_ble_adv_params_t adv_params = {
            .adv_int_min        = 0x20,
            .adv_int_max        = 0x40,
            .adv_type           = ADV_TYPE_IND,
            .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
            .channel_map        = ADV_CHNL_ALL,
            .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        
        esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
        if (ret != ESP_OK) {
            ble_handle_error("开始广播", ret);
        } else {
            s_ble_state = BOARD_BLE_STATE_ADVERTISING;
            ESP_LOGI(BOARD_TAG, "BLE广播已启动");
        }
    } else {
        ESP_LOGW(BOARD_TAG, "当前状态无法开始广播: %s", board_ble_state_to_string(s_ble_state));
    }
}

void board_ble_stop_advertising(void)
{
    if (s_ble_state == BOARD_BLE_STATE_ADVERTISING) {
        ESP_LOGI(BOARD_TAG, "停止BLE广播...");
        
        esp_err_t ret = esp_ble_gap_stop_advertising();
        if (ret != ESP_OK) {
            ble_handle_error("停止广播", ret);
        } else {
            s_ble_state = BOARD_BLE_STATE_INITIALIZED;
            ESP_LOGI(BOARD_TAG, "BLE广播已停止");
        }
    }
}