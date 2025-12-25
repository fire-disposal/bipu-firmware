# 项目结构与功能说明

本项目为基于 ESP-IDF 的嵌入式应用，集成了显示、输入、消息、蓝牙等功能，适用于小型智能终端。核心代码已高度集成与精简，便于维护和扩展。

## 目录结构

```
main/
  ├── main.c           // 程序入口，主循环
  ├── app.c/h          // 应用生命周期与主逻辑
  ├── ui.c/h           // UI 状态管理与接口
  ├── driver.c/h       // 驱动与硬件抽象（定时、输入、BLE、消息、效果等）
  ├── include/         // 头文件归集目录
components/
  └── u8g2/            // u8g2 显示库（保持原样，未做更改）
```

## 各部分功能

- **main/main.c**  
  程序入口，初始化应用，循环调用 `app_loop` 实现主流程。

- **main/app.c, main/include/app.h**  
  应用生命周期管理，负责初始化输入、UI，主循环中处理按键和 UI 刷新。

- **main/ui.c, main/include/ui.h**  
  UI 状态机与消息管理，提供 UI 初始化、状态切换、消息通知、按键响应等接口。

- **main/driver.c, main/include/driver.h**  
  驱动与硬件抽象层，集成定时器、输入（按键）、BLE、消息存储、效果（如震动）等功能，统一对上层提供接口。

- **main/include/**  
  所有头文件集中存放，便于引用和管理。

- **components/u8g2/**  
  第三方 u8g2 显示库，负责 OLED/LCD 等屏幕驱动，未做任何更改，保证兼容性和独立性。

## 构建与依赖

- 使用 ESP-IDF 工具链，CMake 构建。
- 主要依赖：`u8g2`、`freertos`、`esp_timer`、`esp_log`、`esp_rom`、`nvs_flash` 等 ESP-IDF 常用库。

## 说明

- 项目已将所有核心功能合并至 main 目录，极大简化了组件和头文件管理。
- u8g2 相关代码完全保持原样，便于后续升级和维护。
- 推荐在 `main/include` 目录下统一维护头文件，避免重复和遗漏。
