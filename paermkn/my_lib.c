#include "my_lib.h"

// 使用系统库 esp_log.h 定义的宏
static const char *TAG = "MY_LIB";

void print_custom_message(const char* tag)
{
    // 调用系统库函数打印信息
    ESP_LOGI(TAG, "Hello from the custom library component!"); 
    printf("Custom tag received: %s\n", tag);
}