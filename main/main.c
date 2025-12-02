#include <stdio.h>
#include "freertos/FreeRTOS.h" // 引入系统库 FreeRTOS
#include "freertos/task.h"     // 引入系统库 FreeRTOS
#include "my_lib.h"            // 引入自定义库

// ESP-IDF 应用程序的入口点
void app_main(void)
{
    // 1. 使用系统 FreeRTOS 库
    printf("Starting application on core: %d\n", xPortGetCoreID()); 

    // 2. 调用自定义库函数
    print_custom_message("APP_MAIN_TAG");

    // 3. 循环等待
    int count = 0;
    while (1) {
        printf("Loop count: %d\n", count++);
        vTaskDelay(pdMS_TO_TICKS(5000)); // 使用系统库 vTaskDelay
    }
}