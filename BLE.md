这是一份为您专门整理的蓝牙通信接口规范文档。它结合了 **Nordic UART Service (NUS)** 的私有协议封装、**系统级绑定安全机制**以及 **ESP32-C3 (NimBLE)** 的底层实现要求。

---

# 蓝牙通信接口规范文档 (V1.0) - 类寻呼机设备

## 1. 物理层与广播规范 (GAP)

为了保证连接稳定性及系统级列表可见性，设备必须满足以下要求：

* **广播模式**：使用 `General Discoverable Mode` (可发现) 及 `Connectable Mode` (可连接)。
* **设备外观 (Appearance)**：建议设置为 `0x00C0` (Generic Keyring) 或 `0x0180` (Generic Remote Control)。
* **系统列表可见性**：必须在广播数据包 (Advertising Data) 中包含 **Complete Local Name**。
* **自动回连**：支持 **Whitelist Filter Policy**，优先允许已绑定设备连接。

---

## 2. 安全与绑定规范 (Storage & Security)

设备必须支持 **Bonding (绑定)** 并在重启后持久化密钥。

### 2.1 安全参数设置

* **IO Capabilities**: `BLE_HS_IO_NO_INPUT_OUTPUT` (开启 Just Works 配对)。
* **Bonding**: 开启 (`1`)。
* **NVS 存储**: 必须调用 `nvs_flash_init()`，并配置 NimBLE 存储回调以保存 **LTK (长期密钥)**。
* **解绑机制**: 硬件需预留物理组合键或长按逻辑，执行 `ble_store_clear_all()` 以清空 NVS 绑定信息。

### 2.2 权限控制

* **NUS TX Characteristic**: 必须设置为 `BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC`。
> *注：增加 `_ENC` 权限后，App 写入数据时手机系统会自动触发配对弹窗。*



---

## 3. GATT 服务定义

仅保留 **Nordic UART Service (NUS)** 作为数据通道，所有业务逻辑通过协议头区分。

* **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
* **Characteristic (RX/Write)**: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
* **Properties**: `Write`, `Write Without Response`
* **Permissions**: `Encrypted Write`



---

## 4. 应用层通讯协议 (Packet Structure)

所有通过 NUS 写入的数据包均采用：`[Header (1 Byte)] + [Payload (N Bytes)]` 格式。

### 4.1 时间同步协议 (App -> Device)

当 App 连接成功或需要校时时发送。

* **Header**: `0xA1`
* **Payload**: 10 字节 (参考标准 CTS 格式)

| 偏移 | 长度 | 定义 | 说明 |
| --- | --- | --- | --- |
| 0 | 1 | Header | 固定为 `0xA1` |
| 1 | 2 | Year | uint16, 小端序 (e.g., 2026 = `0xEA 0x07`) |
| 3 | 1 | Month | 1 - 12 |
| 4 | 1 | Day | 1 - 31 |
| 5 | 1 | Hour | 0 - 23 |
| 6 | 1 | Minute | 0 - 59 |
| 7 | 1 | Second | 0 - 59 |
| 8 | 1 | Day of Week | 1=Monday...7=Sunday |
| 9 | 1 | Fraction | 1/256th of a second |
| 10 | 1 | Reason | 0 (Manual), 1 (External reference), etc. |

### 4.2 消息传输协议 (App -> Device)

当 App 推送寻呼消息时发送。

* **Header**: `0xA2`
* **Payload**: UTF-8 编码的字符串数据

**分包逻辑说明**：

* App 会根据协商的 **MTU** 进行分包。
* 首包包含 `0xA2`。后续分包不含 Header，直接拼接。
* 硬件端建议使用缓冲区，直到收到完整的字符串（或根据业务约定的结束符判断）。

---

## 5. 稳定性增强建议 (重要)

1. **MTU 协商**：硬件应支持响应手机发起的 MTU Exchange 请求（建议支持至 247 字节）。
2. **底层保活**：由于开启了 `autoConnect` 和 `Bonding`，硬件应维持稳定的广播间隔（建议连接时 20ms - 50ms，待机时 100ms - 500ms）。
3. **连接参数**：为了省电且不失响应速度，建议连接间隔 (Conn Interval) 设定在 `30ms - 100ms` 之间。

---
