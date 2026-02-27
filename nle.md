## 全新架构设计

基于"小而美"原则，我设计一个适合本项目的全新架构：

### 架构概览
```
┌─────────────────────────────────────────────────────────┐
│                   应用层 (app/)                          │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐   │
│  │ 消息管理 │  │ 状态管理 │  │ 事件协调 │  │ 配置管理 │   │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘   │
└─────────────────────────────────────────────────────────┘
                    │              │              │
    ┌───────────────┼──────────────┼──────────────┼───────────────┐
    │              │              │              │               │
┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  │
│  蓝牙层  │  │  UI层   │  │ 存储层  │  │ 硬件层  │  │ 工具层  │  │
│ (ble/)  │  │ (ui/)   │  │(storage/│  │(board/) │  │ (util/) │  │
└─────────┘  └─────────┘  └─────────┘  └─────────┘  └─────────┘  │
    │              │              │              │               │
    └──────────────┴──────────────┴──────────────┴───────────────┘
```

### 详细架构设计

#### 1. 应用层 (app/)
**职责**：系统协调、状态管理、事件分发
```c
// app.h - 应用层接口
#pragma once

// 系统状态
typedef struct {
    bool ble_connected;
    bool charging;
    uint8_t battery_level;
    uint32_t unread_messages;
    time_t last_activity;
} system_state_t;

// 应用层初始化
app_err_t app_init(void);

// 主循环处理
void app_loop(void);

// 事件处理
app_err_t app_handle_ble_message(const char* sender, const char* text, uint32_t timestamp);
app_err_t app_handle_time_sync(uint32_t timestamp);
app_err_t app_handle_connection_change(bool connected);

// 状态查询
const system_state_t* app_get_state(void);
```

#### 2. 蓝牙层 (ble/)
**职责**：蓝牙协议栈、连接管理、数据收发
```c
// ble_manager.h - 简化接口
#pragma once

// 回调函数类型
typedef void (*ble_message_cb_t)(const char* sender, const char* text, uint32_t timestamp);
typedef void (*ble_time_sync_cb_t)(uint32_t timestamp);
typedef void (*ble_connection_cb_t)(bool connected);

// 初始化
esp_err_t ble_init(ble_message_cb_t msg_cb, ble_time_sync_cb_t time_cb, ble_connection_cb_t conn_cb);

// 控制接口
esp_err_t ble_start_advertising(void);
esp_err_t ble_stop_advertising(void);
esp_err_t ble_disconnect(void);

// 状态查询
bool ble_is_connected(void);
const char* ble_get_device_name(void);
```

#### 3. UI层 (ui/)
**职责**：用户界面显示、输入处理、视觉效果
```c
// ui.h - 专注显示
#pragma once

// 纯显示接口
void ui_show_message(const char* sender, const char* text, uint32_t timestamp);
void ui_show_status(const system_state_t* state);
void ui_show_menu(void);

// 输入处理（由app层调用）
void ui_handle_keypress(board_key_t key);

// 控制接口
void ui_set_brightness(uint8_t level);
void ui_enter_standby(void);
void ui_wake_up(void);
```

#### 4. 存储层 (storage/)
**职责**：数据持久化、配置管理
```c
// storage.h - 简化存储接口
#pragma once

// 消息存储
esp_err_t storage_save_message(const ui_message_t* msg);
esp_err_t storage_load_messages(ui_message_t* msgs, int max_count, int* actual_count);

// 配置存储
esp_err_t storage_save_config(const runtime_config_t* config);
esp_err_t storage_load_config(runtime_config_t* config);

// 状态存储（可选）
esp_err_t storage_save_state(const system_state_t* state);
esp_err_t storage_load_state(system_state_t* state);
```

#### 5. 硬件层 (board/)
**职责**：硬件抽象、驱动程序
```c
// board.h - 硬件抽象接口
#pragma once

// 初始化
esp_err_t board_init(void);

// RTC时间
esp_err_t board_set_rtc(uint16_t year, uint8_t month, uint8_t day, 
                       uint8_t hour, uint8_t minute, uint8_t second);
esp_err_t board_get_rtc(struct tm* timeinfo);

// 用户反馈
void board_vibrate_short(void);
void board_vibrate_double(void);
void board_leds_flash(void);
void board_notify(void);

// 电源管理
uint8_t board_get_battery_level(void);
bool board_is_charging(void);
```

#### 6. 工具层 (util/) - 可选
**职责**：通用工具函数
```c
// util.h - 工具函数
#pragma once

// 字符串处理
size_t util_strsafe_copy(char* dest, const char* src, size_t dest_size);
bool util_str_is_printable(const char* str);

// 时间处理
time_t util_adjust_timezone(time_t utc_time, int8_t timezone_hours);
void util_format_time(time_t timestamp, char* buffer, size_t size);

// 调试工具
void util_hex_dump(const char* tag, const uint8_t* data, size_t length);
```

### 迁移计划

#### 第1步：创建新架构骨架（1天）
1. 创建新的目录结构
2. 编写新的头文件定义接口
3. 保持现有代码正常运行

#### 第2步：逐步迁移功能（2-3天）
1. **先迁移硬件层**：将board相关功能整理到新接口
2. **再迁移存储层**：简化存储接口
3. **然后迁移UI层**：移除存储依赖，专注显示
4. **接着迁移蓝牙层**：简化接口，移除业务逻辑
5. **最后实现应用层**：协调所有组件

#### 第3步：测试和优化（1-2天）
1. 编译测试
2. 功能测试
3. 性能测试
4. 内存使用优化

### 迁移策略

#### 策略1：并行开发，逐步替换
```
现有代码 ───┐
            ├──> 临时适配层 ───> 新架构
新架构代码 ──┘
```

#### 策略2：功能模块逐个迁移
```
1. 先迁移时间处理模块
2. 再迁移消息处理模块  
3. 接着迁移蓝牙连接模块
4. 最后迁移UI显示模块
```

### 关键优势

1. **清晰的分层**：每层职责明确，易于理解和维护
2. **松耦合**：层间通过接口通信，便于测试和替换
3. **易于扩展**：添加新功能只需在相应层实现
4. **资源友好**：避免过度设计，保持小型项目特点
5. **迁移平滑**：可以逐步迁移，不影响现有功能

## 完整文件树规划
bipupu_esp/
├── CMakeLists.txt                    # 根项目CMake配置
├── sdkconfig                         # ESP-IDF SDK配置
├── partitions.csv                    # Flash分区表
├── README.md                         # 更新项目文档
├── main/
│   ├── CMakeLists.txt               # 主程序CMake配置
│   ├── main.c                       # 程序入口（简化版）
│   └── idf_component.yml           # ESP-IDF组件配置
└── components/
    ├── app/                         # 应用层（协调者）
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── app.h               # 应用层主接口
    │   │   ├── app_config.h        # 配置管理
    │   │   ├── app_state.h         # 状态管理
    │   │   ├── app_message.h       # 消息管理
    │   │   └── app_event.h         # 事件协调
    │   └── src/
    │       ├── app.c               # 应用层主实现
    │       ├── app_config.c        # 配置管理实现
    │       ├── app_state.c         # 状态管理实现
    │       ├── app_message.c       # 消息管理实现
    │       └── app_event.c         # 事件协调实现
    ├── ble/                         # 蓝牙层
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── ble_manager.h       # 蓝牙管理器接口
    │   │   └── bipupu_protocol.h   # Bipupu协议定义
    │   └── src/
    │       ├── ble_manager.c       # 蓝牙管理器实现
    │       └── bipupu_protocol.c   # 协议解析实现
    ├── ui/                          # UI层
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── ui.h                # UI主接口
    │   │   ├── ui_types.h          # UI数据类型
    │   │   ├── ui_display.h        # 显示接口
    │   │   └── ui_input.h          # 输入处理接口
    │   └── src/
    │       ├── ui.c                # UI主实现
    │       ├── ui_display.c        # 显示实现
    │       ├── ui_input.c          # 输入处理实现
    │       └── ui_pages/           # 各页面实现
    │           ├── ui_page_main.c      # 主页面
    │           ├── ui_page_message.c   # 消息页面
    │           ├── ui_page_list.c      # 列表页面
    │           └── ui_page_settings.c  # 设置页面
    ├── storage/                     # 存储层
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── storage.h           # 存储主接口
    │   │   ├── storage_message.h   # 消息存储接口
    │   │   └── storage_config.h    # 配置存储接口
    │   └── src/
    │       ├── storage.c           # 存储主实现
    │       ├── storage_message.c   # 消息存储实现
    │       └── storage_config.c    # 配置存储实现
    ├── board/                       # 硬件层
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── board.h             # 硬件抽象接口
    │   │   ├── board_rtc.h         # RTC接口
    │   │   ├── board_power.h       # 电源管理接口
    │   │   ├── board_input.h       # 输入设备接口
    │   │   ├── board_output.h      # 输出设备接口
    │   │   └── board_display.h     # 显示设备接口
    │   └── src/
    │       ├── board.c             # 硬件抽象主实现
    │       ├── board_rtc.c         # RTC实现
    │       ├── board_power.c       # 电源管理实现
    │       ├── board_input.c       # 输入设备实现
    │       ├── board_output.c      # 输出设备实现
    │       └── board_display.c     # 显示设备实现
    ├── util/                        # 工具层（可选）
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   ├── util_string.h       # 字符串工具
    │   │   ├── util_time.h         # 时间工具
    │   │   ├── util_debug.h        # 调试工具
    │   │   └── util_memory.h       # 内存工具
    │   └── src/
    │       ├── util_string.c
    │       ├── util_time.c
    │       ├── util_debug.c
    │       └── util_memory.c
    └── u8g2/                        # 第三方显示库（保持不变）
        └── ...                     # 原有u8g2文件结构
