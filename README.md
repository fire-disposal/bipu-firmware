# Bipupu Firmware

给 Bipupu App 配的那块小屏固件：ESP32-C3 接 128×64 OLED、四个按键和振动马达，通过 BLE 收手机发来的文字消息并存在本地。

**状态**：可用（v1.2.0，需手动烧录）；**入口**：`idf.py build flash monitor`；**仓库**：本仓库；App 与后端在 [bipu](https://github.com/fire-disposal/bipu)

## 本质

驱动 ESP32-C3 小屏、通过 BLE 接收并保存 Bipupu 消息的固件。

## 选型

基于 ESP-IDF 的 ESP32-C3 单机固件，MCU ESP32-C3，屏 128×64 I2C OLED，协议栈 Bluedroid + NUS + 0xB0，驱动 u8g2 + FreeRTOS，存储 NVS。

- MCU
  - ESP32-C3
- 屏幕
  - SSD1306 128×64
    - u8g2
- 协议栈
  - Bluedroid
    - Nordic UART Service
    - Bipupu 协议 v1.2
  - I2C
- RTOS
  - FreeRTOS
    - app_task
    - gui_task
- 存储
  - NVS
- 外设
  - 按键
  - 振动马达
  - LED
  - ADC
- 工具链
  - ESP-IDF 5.5.2
  - CMake 3.16

## 功能

- 五个页面：主界面（时钟 + 未读数）、消息列表、消息阅读、设置、屏保/黑屏
- 消息最多 10 条，发送者 32 字节、正文 128 字节，支持滚动、切换、删除、未读计数
- 设置项：亮度（0–100，写 NVS）、手电筒、锁屏、解绑、重启、关于
- BLE 设备名 `Bipupu_XXXX`，MTU 517、协议载荷上限 240；未绑定时只放行绑定/解绑指令
- 电池每 10 秒 ADC 采样并按 0.25 系数平滑，2.5–5.0V 视为有效区间，可判断充电
- 未实现：OTA（分区表只有 `factory` + `otadata`）、Light Sleep（无 `esp_sleep_*` 调用）

## 启动流程

```bash
git clone --recurse-submodules https://github.com/fire-disposal/bipu-firmware.git
cd bipu-firmware
git submodule update --init --recursive   # u8g2 是 submodule

. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

接线（`components/board/include/board_pins.h`）：

| 功能 | GPIO |
|---|---|
| I2C SDA / SCL | 21 / 20 |
| 按键 UP / DOWN / ENTER / BACK | 5 / 6 / 7 / 8 |
| 振动马达 | 10 |
| 电池采样 | 4 |
| LED 0 / 1 / 2 | 0 / 1 / 3 |
| 屏幕复位 | 2 |

## 边界与已知问题

- 旧 README 与代码不符，以代码为准：消息格式是二进制 0xB0 而非 JSON；没有 OTA 与失败回滚；没有 Light Sleep；项目结构在 `components/` 而非 `main/*.c`
- `main/kalman_health_demo.c` 未被 `main/CMakeLists.txt` 编译，是遗留文件
- 消息容量固定 10 条、正文 128 字节，没有按时间自动清理
- 阅读页的确认键是“切下一条”，删除只在列表页，和旧文档描述不一致
- 屏保与黑屏只是软件关显示，实测电流与续航数据缺失
- 绑定只比对手机 MAC 字符串，绑定包里的 JSON 只写日志，没有配对密钥交换
