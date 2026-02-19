#include "ble_manager.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "ui.h"
#include "ui_render.h"
#include "soc/rtc.h"


static const char *MAIN_TAG = "MAIN_BOOT";

/* ======================== 任务配置 ======================== */

#define APP_TASK_STACK_SIZE (4096)
#define APP_TASK_PRIORITY (5)
#define APP_TASK_PERIOD_MS (20) // 50Hz 平滑硬件驱动
#define APP_TASK_NAME "app_task"
#define GUI_TASK_STACK_SIZE (4096)
#define GUI_TASK_PRIORITY (3)   // 优先级略低于逻辑任务
#define GUI_TASK_PERIOD_MS (40) // 25FPS
#define GUI_TASK_NAME "gui_task"

#define STARTUP_RESTART_DELAY_MS (2000)

/* ======================== 任务句柄 ======================== */
static TaskHandle_t s_gui_task_handle = NULL;
static TaskHandle_t s_app_task_handle = NULL;

/* ======================== 任务实现 ======================== */

// GUI 渲染任务：负责屏幕刷新
static void gui_task(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    ui_tick();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(40)); // 25FPS
  }
}

// APP 逻辑任务：负责按键、LED、电量同步
static void app_task(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint32_t cnt = 0;
  for (;;) {
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE)
      ui_on_key(key);

    board_vibrate_tick();
    board_leds_tick();

    // 每 200ms 执行一次低频同步（电量、LED模式）
    if (++cnt >= 10) {
      cnt = 0;
      // 状态同步逻辑
      ble_state_t ble_st = ble_manager_get_state();
      if (ui_is_flashlight_on())
        board_leds_set_mode(BOARD_LED_MODE_STATIC);
      else
        board_leds_set_mode(ble_st == BLE_STATE_CONNECTED ? BOARD_LED_MODE_BLINK
                                                          : BOARD_LED_MODE_OFF);

      board_battery_manager_tick();
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
  }
}

/* ======================== 主入口 ======================== */

void app_main(void) {
  // 1. 【视觉优先】—— 剥离非核心初始化
  ESP_LOGI(MAIN_TAG, "Initializing I2C...");
  if (board_i2c_init() != ESP_OK) {
    ESP_LOGE(MAIN_TAG, "I2C initialization failed");
    // 继续启动，后续对 I2C 的调用需自行检查句柄
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing Display...");
  board_display_init(); 
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing UI...");
  ui_init();
  vTaskDelay(pdMS_TO_TICKS(500));

  // 在 GUI 任务启动前显示开机 LOGO（显示已初始化且未并发访问）
  ui_render_logo();
  vTaskDelay(pdMS_TO_TICKS(800));

  xTaskCreatePinnedToCore(gui_task, "gui_task", 4096, NULL, 3,
                          &s_gui_task_handle, 0);

  // 2. 【静默初始化其它硬件】—— 屏幕已经亮了，用户不再焦虑，延迟可以给足
  ESP_LOGI(MAIN_TAG, "Initializing Keys...");
  board_key_init();
  vTaskDelay(pdMS_TO_TICKS(500));
  // board_leds_init();
  // vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing Vibrator...");
  board_vibrate_init();
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(MAIN_TAG, "Initializing Power...");
  board_power_init();
  vTaskDelay(pdMS_TO_TICKS(500));

  // 3. 【存储加载】NVS 和文件系统
  // 此时主频默认 160MHz，Flash 读取电流约 30-50mA
  ESP_LOGI(MAIN_TAG, "Initializing Storage...");
  if (storage_init() != ESP_OK) {
    ESP_LOGW(MAIN_TAG, "Storage init failed, using default config");
  }
  vTaskDelay(pdMS_TO_TICKS(500));

  // 4. 【环境感知】已移除供电模式分支，保持固定日志级别

  // 5. 【逻辑启动】开启应用任务
  xTaskCreatePinnedToCore(app_task, "app_task", 4096, NULL, 5,
                          &s_app_task_handle, 0);

  // 6. 【电源稳压】在启动蓝牙大魔王前，给系统一个缓冲期
  // 此时屏幕和逻辑任务已稳定运行，电流平稳
  vTaskDelay(pdMS_TO_TICKS(500));

  // 7. 【高能耗动作】分步初始化电池管理和蓝牙
  ESP_LOGI(MAIN_TAG, "Initializing Battery Manager...");
  board_battery_manager_init();

  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGI(MAIN_TAG, "Attempting BLE Radio Launch...");
  // 重点：这是 BOD 重启的最可能触发点
  if (ble_manager_init() == ESP_OK) {
    ble_manager_start_advertising();
    ESP_LOGI(MAIN_TAG, "BLE Stack Online");
  } else {
    // 如果蓝牙炸了，至少 UI 和其他功能还能用（降级模式）
    ESP_LOGE(MAIN_TAG, "BLE Radio Failed to start");
  }

  // 8. 【清理退出】
  ESP_LOGI(MAIN_TAG, "Boot Sequence Complete.");
  vTaskDelete(NULL);
}