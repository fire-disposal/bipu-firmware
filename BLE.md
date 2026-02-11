# 蓝牙协议细节文档

## 概述

本协议基于 BLE (Bluetooth Low Energy) 实现 Flutter 应用与 ESP32 设备的通信。主要用于消息转发，支持可选的电池监控和时间同步功能。

## BLE 服务和特征

### 1. Nordic UART Service (NUS) - 消息转发服务
**服务 UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`

#### 特征详情

| 特征名称 | UUID | 属性 | 描述 | 数据类型 |
|----------|------|------|------|----------|
| TX (发送) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | Write (Without Response) | Flutter 应用写入消息数据到设备 | UTF-8 字节数组 |
| RX (接收) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | Notify, Read | 设备发送响应或数据到 Flutter 应用 | UTF-8 字节数组 |

### 2. 电池服务 (Battery Service) - 可选
**服务 UUID**: `0000180f-0000-1000-8000-00805f9b34fb`

#### 特征详情

| 特征名称 | UUID | 属性 | 描述 | 数据类型 |
|----------|------|------|------|----------|
| 电池电量 | `00002a19-0000-1000-8000-00805f9b34fb` | Notify, Read | 报告电池电量百分比 | uint8 (0-100) |

### 3. 当前时间服务 (Current Time Service) - 可选
**服务 UUID**: `00001805-0000-1000-8000-00805f9b34fb`

#### 特征详情

| 特征名称 | UUID | 属性 | 描述 | 数据类型 |
|----------|------|------|------|----------|
| 当前时间 | `00002a2b-0000-1000-8000-00805f9b34fb` | Write, Read | 同步设备时间 | Exact Time 256 (10 字节) |

## 数据格式规范

### 消息数据格式
- **编码**: UTF-8
- **最大长度**: 无限制（自动分块传输）
- **分块大小**: MTU - 3 字节（默认 MTU=23, 分块=20 字节）
- **示例**: `"From John: Hello World"`

### 电池电量数据格式
- **类型**: uint8
- **范围**: 0-100 (%)
- **字节序**: Little Endian

### 时间数据格式 (Exact Time 256)
- **长度**: 10 字节
- **结构**:
  - 字节 0-1: 年 (uint16, Little Endian)
  - 字节 2: 月 (uint8)
  - 字节 3: 日 (uint8)
  - 字节 4: 时 (uint8)
  - 字节 5: 分 (uint8)
  - 字节 6: 秒 (uint8)
  - 字节 7: 星期 (uint8, 1=周一...7=周日)
  - 字节 8: 毫秒高位 (uint8, 毫秒 * 256 / 1000)
  - 字节 9: 调整原因 (uint8, 通常为 0)

## 接口定义

### BluetoothDeviceService 类接口

#### 主要方法

| 方法名 | 参数 | 返回值 | 描述 |
|--------|------|--------|------|
| `connect(BluetoothDevice device)` | `BluetoothDevice` | `Future<void>` | 连接到 BLE 设备 |
| `disconnect()` | 无 | `Future<void>` | 断开 BLE 连接 |
| `forwardMessage(String message)` | `String` | `Future<void>` | 转发消息到设备 |

#### 状态监听器

| 监听器 | 类型 | 描述 |
|--------|------|------|
| `connectionState` | `ValueNotifier<BluetoothConnectionState>` | 连接状态变化通知 |
| `batteryLevel` | `ValueNotifier<int?>` | 电池电量变化通知 |

## 通信流程

### 连接建立
1. 扫描 BLE 设备
2. 发现目标设备（通过服务 UUID 过滤）
3. 建立连接
4. 发现服务和特征
5. 设置通知（电池电量、RX 特征）

### 消息转发
1. 接收应用层消息
2. UTF-8 编码
3. 根据 MTU 分块
4. 逐块写入 TX 特征
5. 设备接收并重组消息

### 时间同步（可选）
1. 获取当前时间
2. 格式化为 Exact Time 256
3. 写入当前时间特征

## 技术参数

- **BLE 版本**: 4.0+
- **默认 MTU**: 23 字节
- **最大分块大小**: MTU - 3 = 20 字节
- **连接超时**: 15 秒
- **消息轮询间隔**: 前台 15 秒，后台 5 分钟

## 错误处理

- 连接失败：自动重试或通知用户
- 写入失败：记录日志，不中断流程
- MTU 协商失败：使用默认值

## 兼容性

- **Flutter 端**: Flutter Blue Plus 库
- **ESP32 端**: ESP-IDF NimBLE 栈
- **Android/iOS**: 支持 BLE 4.0+ 设备</content>
<parameter name="filePath">d:\code\project\bipupu_project\bipupu\flutter_user\bluetooth_protocol_details.md