#ifndef UI_INPUT_H
#define UI_INPUT_H

/**
 * @brief 按键事件：下（下一项）
 */
void ui_input_on_key_down(void);

/**
 * @brief 按键事件：上（上一项）
 */
void ui_input_on_key_up(void);

/**
 * @brief 按键事件：确认（进入/确认）
 */
void ui_input_on_key_enter(void);

/**
 * @brief 按键事件：返回（退出当前项）
 */
void ui_input_on_key_back(void);

/**
 * @brief 初始化按键 GPIO 中断（GPIO 10/11/12/13，上拉输入，上升沿触发）
 */
void ui_input_init(void);

#endif // UI_INPUT_H