#include "board.h"
#include "ui.h"

void app_init(void) {
    // 初始化震动马达
    board_vibrate_init();
    
    // 初始化RGB灯
    board_rgb_init();
    
    // UI初始化
    ui_init();
}

void app_loop(void) {
    // 轮询按键事件
    board_key_t key = board_key_poll();
    if (key != BOARD_KEY_NONE) {
        board_notify(); // 按键反馈
        ui_on_key(key);
    }
    
    // UI主循环
    ui_tick();
}