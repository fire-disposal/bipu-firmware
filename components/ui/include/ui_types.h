#pragma once
// storage_message_t 是数据的正式定义（存储层拥有），此处仅创建别名以保持 UI 层代码不变。
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

// ui_message_t 是 storage_message_t 的透明别名，硬件权威 API 統一使用 storage_message_t
typedef storage_message_t ui_message_t;

#ifdef __cplusplus
}
#endif
