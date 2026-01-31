#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 渲染状态栏（包含蓝牙图标、居中文本/时间、以及电池显示）
 * @param center_text 居中显示的文本（NULL 表示显示当前时间）
 */
void ui_status_render(const char* center_text);

#ifdef __cplusplus
}
#endif
