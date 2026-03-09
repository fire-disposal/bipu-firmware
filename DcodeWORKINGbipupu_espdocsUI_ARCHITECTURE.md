# UI 系统架构梳理

## 1. UI 系统概览

```
┌─────────────────────────────────────────────────────────┐
│                    UI 系统架构                           │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  app.c (app_task, 10ms 周期)                              │
│    └─> app_loop()                                      │
│         ├─> board_key_poll()                           │
│         │    └─> ui_on_key()  ← 按键处理               │
│         ├─> ui_flush_pending_saves()  ← NVS 持久化      │
│         └─> ...其他模块 tick                            │
│                                                         │
│  app.c (gui_task, 独立任务)                             │
│    └─> gui_task()                                      │
│         └─> ui_tick()  ← UI 逻辑 + 渲染调度             │
│              ├─> ui_lock()  ← 互斥锁保护               │
│              ├─> 页面 update()                          │
│              ├─> 检查待机超时                           │
│              └─> 页面 render()  ← 无锁渲染             │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## 2. 核心数据结构

### 2.1 UI 状态枚举 (`ui.h:15-21`)
```c
typedef enum {
    UI_STATE_STANDBY,      // 待机黑屏
    UI_STATE_MAIN,         // 主界面（时钟/状态）
    UI_STATE_MESSAGE_LIST, // 消息列表
    UI_STATE_MESSAGE_READ, // 消息阅读
    UI_STATE_SETTINGS,     // 设置页面
} ui_state_enum_t;
```

### 2.2 UI 上下文 (`ui.c:86-96`)
```c
typedef struct {
    ui_state_enum_t state;           // 当前页面状态
    ui_message_t messages[MAX_MESSAGES];  // 消息数组
    int message_count;               // 消息数量
    int current_msg_idx;             // 当前选中索引
    uint32_t last_activity_time;     // ⭐ 最后活动时间戳（用于待机判断）
    bool flashlight_on;              // 手电筒状态
    uint8_t brightness;              // OLED 亮度
} ui_context_t;
```

### 2.3 页面接口 (`ui_page.h:4-10`)
```c
typedef struct {
    void (*on_enter)(void);     // 进入页面回调
    void (*on_exit)(void);      // 退出页面回调
    uint32_t (*update)(void);   // 逻辑更新（返回下次刷新间隔 ms）
    void (*render)(void);       // 渲染
    void (*on_key)(board_key_t key);  // 按键处理
} ui_page_t;
```

## 3. 关键函数流程

### 3.1 UI 初始化流程
```
ui_init() (ui.c:172)
├─> 创建 UI 互斥锁
├─> 加载 NVS 数据（消息、亮度）
├─> 设置初始状态：UI_STATE_MAIN
├─> 调用页面 on_enter()
└─> 注册 Toast 预刷新钩子
```

### 3.2 UI Tick 流程（GUI 任务调用）
```
ui_tick() (ui.c:213)
├─> ui_lock()  ← 获取互斥锁
├─> 检查 Toast 超时
├─> 检查待机超时 (STANDBY_TIMEOUT_MS = 30000ms)
│    └─> 超时：ui_enter_standby()
├─> 调用当前页面的 update()
├─> ui_unlock()  ← 释放互斥锁
└─> 执行渲染（无锁状态）
     ├─> STANDBY: ui_render_standby()
     └─> 其他：页面 render()
```

### 3.3 按键处理流程
```
ui_on_key(key) (ui.c:278)
├─> ui_lock()  ← 获取互斥锁
├─> ui_update_activity()  ← ⭐ 刷新活动时间戳
├─> Toast 拦截（有 Toast 则直接关闭）
├─> STANDBY 状态：
│    └─> ui_wake_up() → 切换到 MAIN
├─> 调用页面的 on_key()
├─> ui_request_redraw()
└─> ui_unlock()  ← 释放互斥锁
```

### 3.4 待机进入流程
```
ui_enter_standby() (ui.c:379)
├─> ui_change_page(UI_STATE_STANDBY)
│    ├─> 调用旧页面 on_exit()
│    ├─> 切换状态
│    ├─> 调用新页面 on_enter()
│    └─> ui_request_redraw()
├─> ui_render_standby()  ← 播放屏保动画
└─> board_leds_off()  ← 关闭 LED
```

## 4. 页面实现

### 4.1 页面文件结构
```
components/ui/src/
├── ui_page_main.c      // 主界面（时钟）
├── ui_page_list.c      // 消息列表
├── ui_page_message.c   // 消息阅读
└── ui_page_settings.c  // 设置页面
```

### 4.2 各页面功能

| 页面 | update() | render() | on_key() |
|------|----------|----------|----------|
| **MAIN** | 1000ms 更新消息计数 | 时钟 + 日期 + 消息数 | UP→设置，ENTER/DOWN→消息列表 |
| **LIST** | 无逻辑 | 消息列表（可滚动） | UP/DOWN 滚动，ENTER 阅读，BACK 返回 |
| **MESSAGE** | 无逻辑 | 消息内容（可滚动） | UP/DOWN 滚动，BACK 返回，ENTER 删除 |
| **SETTINGS** | 无逻辑 | 设置项（亮度、系统） | UP/DOWN 选择，ENTER 确认 |

## 5. 与 Light Sleep 相关的关键点

### 5.1 活动时间追踪
**位置**: `ui.c:124`
```c
static void ui_update_activity(void) {
    s_ui.last_activity_time = board_time_ms();
}
```
**调用点**:
- `ui_on_key()` 每次按键时
- `ui_show_message_with_timestamp()` 收到新消息时

**Light Sleep 集成**: 可从此函数触发唤醒通知

### 5.2 待机超时判断
**位置**: `ui.c:238`
```c
#define STANDBY_TIMEOUT_MS 30000  // ⚠️ 需要修改为 60000

if (board_time_ms() - s_ui.last_activity_time > STANDBY_TIMEOUT_MS) {
    ui_enter_standby();
}
```

**Light Sleep 集成**: 需要增加第二阶段超时（黑屏后进入 Light Sleep）

### 5.3 唤醒入口
**位置**: `ui.c:397`
```c
void ui_wake_up(void) {
    if (s_ui.state == UI_STATE_STANDBY) {
        ui_change_page(UI_STATE_MAIN);
        ui_update_activity();
    }
}
```

**Light Sleep 集成**: 从 Light Sleep 唤醒后需要调用此函数

### 5.4 显示控制
**位置**: `board_display_set_contrast()` (display.c:248)
```c
void board_display_set_contrast(uint8_t contrast) {
    // 控制 OLED 亮度（0=关闭，100=最亮）
}
```

**Light Sleep 集成**: 黑屏时调用 `board_display_set_contrast(0)`

## 6. 互斥锁保护机制

### 6.1 锁保护范围
```c
// ui_tick() 持有锁期间：
- 读取 UI 状态
- 调用页面 update()
- 检查待机超时
- 决定是否需要渲染

// 渲染过程（无锁）：
- board_display_begin()
- 页面 render()
- board_display_end()
```

### 6.2 死锁预防
- `ui_lock()` 超时 200ms，失败则跳过本次 tick
- `ui_on_key()` 获取锁失败时丢弃按键
- 渲染在锁外执行，避免阻塞按键响应

## 7. 延迟 NVS 持久化

### 7.1 机制说明
**问题**: NVS 写入需要 10-50ms，在锁内执行会阻塞 UI 响应

**解决方案**: 延迟写入
```c
// ui.c:33-42
static bool s_deferred_msg_save = false;
static bool s_deferred_brightness_save = false;

// 锁内：仅快照数据
memcpy(s_save_snap, s_ui.messages, ...);
s_deferred_msg_save = true;

// 锁外（app_loop 调用）：
ui_flush_pending_saves();  // 实际写入 NVS
```

### 7.2 调用时机
**app.c:121**
```c
void app_loop(void) {
    ui_flush_pending_saves();  // ← 每 10ms 检查一次
}
```

## 8. Toast 覆盖层机制

### 8.1 实现原理
**预刷新钩子**: `board_display_set_pre_flush_cb()`
```c
// ui.c:51-55
static void toast_pre_flush_cb(void) {
    if (s_toast_visible) {
        ui_render_toast_overlay(s_toast_msg);
    }
}
```

### 8.2 渲染时机
```
board_display_end()
└─> 调用 pre_flush_cb()
    └─> 绘制 Toast 覆盖层
└─> u8g2_SendBuffer()
```

## 9. Light Sleep 集成建议

### 9.1 修改点清单

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `ui.c:30` | `STANDBY_TIMEOUT_MS` | 30000 → 60000 |
| `ui.c:238-243` | 待机超时逻辑 | 增加第二阶段判断（黑屏 → Light Sleep） |
| `ui.h` | 新增接口 | `ui_get_last_activity_time()` |
| `ui.c:397` | `ui_wake_up()` | 增加唤醒提示（Toast/震动） |

### 9.2 新增接口
```c
// ui.h
uint32_t ui_get_last_activity_time(void);  // 获取最后活动时间
void ui_enter_black_screen(void);          // 仅关闭显示（不播放动画）
void ui_wake_from_sleep(void);             // 从睡眠唤醒（带提示）
```

### 9.3 电源状态机集成
```c
// app.c 新增
#define BLACK_SCREEN_TIMEOUT_MS   (60000)   // 60 秒黑屏
#define LIGHT_SLEEP_TIMEOUT_MS    (300000)  // 5 分钟睡眠

void power_management_tick(void) {
    uint32_t idle_time = board_time_ms() - ui_get_last_activity_time();
    
    if (idle_time > LIGHT_SLEEP_TIMEOUT_MS) {
        board_sleep_enter(POWER_STATE_LIGHT_SLEEP);
    } else if (idle_time > BLACK_SCREEN_TIMEOUT_MS) {
        if (ui_get_state() != UI_STATE_STANDBY) {
            ui_enter_black_screen();  // 仅关闭显示
        }
    }
}
```

## 10. 数据流图

```
┌─────────────┐
│ BLE 消息     │
│  到达        │
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────┐
│ ble_manager_process_pending_   │
│ messages()                      │
│   └─> 回调 s_message_callback  │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ ui_show_message_with_timestamp │
│   ├─> 添加到消息数组            │
│   ├─> ui_wake_up()             │
│   ├─> ui_change_page(READ)     │
│   ├─> storage_save_messages()  │
│   └─> board_notify()  ← 震动  │
└─────────────────────────────────┘
```

## 11. 关键时序图

```
时间轴 (ms):
0       10      20      30      40      50      60
│───────│───────│───────│───────│───────│───────│
app_task:
  ├─ app_loop() 每 10ms
  │   ├─ key_poll()
  │   ├─ ui_flush_pending_saves()
  │   └─ ble_process()
  │
gui_task:
  └─ ui_tick() 每 50-1000ms
      ├─ 页面 update()
      └─ 页面 render()
```

## 12. 内存占用分析

| 组件 | 大小 | 说明 |
|------|------|------|
| `ui_context_t` | ~2KB | 消息数组 MAX_MESSAGES×sizeof(ui_message_t) |
| UI 互斥锁 | ~100B | FreeRTOS 信号量 |
| 页面栈 | ~4KB | GUI 任务栈空间 |
| **总计** | **~6KB** | 占用 RTC 快速内存 |

**Light Sleep 影响**: 所有变量在 Light Sleep 期间保持（无需特殊处理）
