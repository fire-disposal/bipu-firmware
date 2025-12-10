# AstraUI Lite ESP-IDF 集成方案

本项目实现了 AstraUI Lite 与 u8g2_hal_esp_idf 在 ESP-IDF 环境下的稳健集成，提供了一个结构清晰、安全可靠的 UI 框架使用范例。

## 🎯 设计目标

- **架构清晰**：采用分层设计，职责分离
- **代码安全**：完善的错误处理和状态管理
- **易于维护**：模块化设计，便于扩展和修改
- **性能优化**：合理的任务划分和资源管理
- **文档完善**：详细的代码注释和使用说明

## 📁 项目结构

```
project/
├── main/
│   ├── main.c              # 主程序，包含完整的集成实现
│   ├── CMakeLists.txt      # 主组件编译配置
│   └── idf_component.yml   # 组件依赖配置
├── components/
│   ├── astraui_lite/       # AstraUI Lite UI 框架
│   ├── u8g2/               # u8g2 图形库
│   └── u8g2_hal_esp_idf/   # u8g2 ESP-IDF 硬件抽象层
├── CMakeLists.txt          # 项目根配置
└── README.md              # 本文档
```

## 🔧 硬件配置

### OLED 显示屏
- **接口类型**：I2C
- **分辨率**：128x64
- **GPIO 配置**：
  - SDA: GPIO_NUM_20
  - SCL: GPIO_NUM_21

### 按键输入
- **按键数量**：4 个
- **GPIO 配置**：
  - KEY1: GPIO_NUM_10
  - KEY2: GPIO_NUM_11
  - KEY3: GPIO_NUM_12
  - KEY4: GPIO_NUM_13
- **按键特性**：低电平触发，内置上拉电阻

## 🚀 快速开始

### 1. 环境准备
```bash
# 确保已安装 ESP-IDF
source $IDF_PATH/export.sh

# 克隆项目
git clone <your-repo-url>
cd astraui-lite-espidf
```

### 2. 编译和烧录
```bash
# 配置目标芯片
idf.py set-target esp32s3  # 根据你的芯片型号调整

# 编译项目
idf.py build

# 烧录到设备
idf.py -p /dev/ttyUSB0 flash

# 监控串口输出
idf.py -p /dev/ttyUSB0 monitor
```

## 📋 功能特性

### UI 元素支持
- ✅ **列表项**：基础导航元素
- ✅ **开关项**：布尔值切换控件
- ✅ **滑块项**：数值调节控件（带步进）
- ✅ **按钮项**：动作触发控件
- ✅ **用户自定义项**：完全自定义的界面

### 交互特性
- ✅ **长按进入**：长按任意键 1.5 秒进入 UI
- ✅ **平滑动画**：所有界面切换都有过渡动画
- ✅ **状态保持**：UI 状态在切换过程中保持
- ✅ **信息提示**：支持顶部信息栏和弹窗提示

### 系统特性
- ✅ **错误处理**：完善的错误检测和处理机制
- ✅ **任务分离**：按键处理和显示渲染分离
- ✅ **资源管理**：合理的内存和任务管理
- ✅ **日志系统**：详细的运行日志输出

## 🔍 代码架构

### 分层设计

```
┌─────────────────────────────────────┐
│           应用层 (main.c)            │
│  - 系统初始化  - 任务管理  - UI 集成  │
├─────────────────────────────────────┤
│           UI 框架层                  │
│  - AstraUI Lite 核心逻辑和渲染      │
├─────────────────────────────────────┤
│           驱动抽象层                 │
│  - u8g2 图形库  - HAL 硬件抽象      │
├─────────────────────────────────────┤
│           硬件层                     │
│  - ESP32 外设  - OLED 显示屏        │
└─────────────────────────────────────┘
```

### 关键改进

1. **驱动初始化外置**：将 OLED 初始化从 UI 框架中移除，避免重复初始化
2. **任务分离**：按键扫描和显示渲染分别在不同的任务中执行
3. **状态管理**：完善的系统状态跟踪和错误处理
4. **配置集中**：所有硬件配置集中管理，便于修改

## ⚙️ 配置选项

在 [`main.c`](main/main.c) 中可以修改以下配置：

### 硬件配置
```c
#define OLED_I2C_SDA_GPIO    GPIO_NUM_20    // I2C SDA 引脚
#define OLED_I2C_SCL_GPIO    GPIO_NUM_21    // I2C SCL 引脚
#define BUTTON_COUNT         4              // 按键数量
#define BUTTON_GPIO_PINS     {GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13}
```

### 系统配置
```c
#define BUTTON_DEBOUNCE_MS   50             // 按键消抖时间
#define BUTTON_LONG_PRESS_MS 1500           // 长按检测时间
#define UI_REFRESH_INTERVAL_MS   50         // UI 刷新间隔
```

### 任务配置
```c
#define DISPLAY_TASK_STACK_SIZE  8192       // 显示任务栈大小
#define DISPLAY_TASK_PRIORITY    5          // 显示任务优先级
#define BUTTON_TASK_STACK_SIZE   4096       // 按键任务栈大小
#define BUTTON_TASK_PRIORITY     3          // 按键任务优先级
```

## 🛠️ 扩展开发

### 添加新的 UI 元素
1. 在 [`astra_ui_item.h`](components/astraui_lite/Source_code/astra_ui_item.h) 中定义新的元素类型
2. 实现对应的创建和渲染函数
3. 在 [`main.c`](main/main.c) 的 `create_ui_menu()` 中添加使用示例

### 修改硬件配置
1. 更新 [`main.c`](main/main.c) 中的硬件配置宏定义
2. 确保新的 GPIO 引脚与硬件连接匹配
3. 重新编译并测试

### 添加新的交互方式
1. 在 `button_task()` 中添加按键扫描逻辑
2. 在 `process_button_input()` 中添加交互处理
3. 在 `handle_ui_navigation()` 中添加导航逻辑

## 🔧 故障排除

### 常见问题

1. **OLED 不显示**
   - 检查 I2C 引脚连接是否正确
   - 确认 OLED 模块的 I2C 地址
   - 检查电源供应是否稳定

2. **按键无响应**
   - 验证 GPIO 引脚配置
   - 检查按键电路连接
   - 确认按键消抖参数

3. **系统崩溃**
   - 检查任务栈大小是否足够
   - 验证内存使用情况
   - 查看串口日志输出

### 调试建议

1. **启用详细日志**：修改 `menuconfig` 中的日志级别
2. **使用 GDB 调试**：通过 JTAG 接口进行硬件调试
3. **内存监控**：使用 `esp_get_free_heap_size()` 监控内存使用

## 📚 相关资源

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/)
- [u8g2 文档](https://github.com/olikraus/u8g2)
- [AstraUI Lite 源码](components/astraui_lite/)

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request 来改进这个项目。在提交之前，请确保：

1. 代码遵循项目的编码规范
2. 添加了适当的注释和文档
3. 通过了基本的测试验证
4. 更新了相关的文档

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 🙏 致谢

- [AstraUI Lite](https://github.com/yourusername/astraui-lite) 项目提供优秀的 UI 框架
- [u8g2](https://github.com/olikraus/u8g2) 项目提供强大的图形库
- [ESP-IDF](https://github.com/espressif/esp-idf) 项目提供完整的开发框架