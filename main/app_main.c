#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP-IDF 程序入口，只允许调用 app_init/app_loop/vTaskDelay
void app_main(void) {
    app_init();
    while (1) {
        app_loop();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}