# 蓝牙固件参考文档

本文档为固件工程师提供Flutter应用中蓝牙通信相关的关键代码摘要，包括电量信息传输和时间同步功能。

## 1. 蓝牙服务和特征值UUID

```dart
// 主要服务UUID
static const String serviceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const String writeCharUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static const String notifyCharUuid = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";

// 标准电池服务
static const String batteryServiceUuid = "180F";
static const String batteryLevelCharUuid = "2A19";
```

## 2. 电量信息传输

### 2.1 电量服务发现和处理

```dart
Future<void> _handleBatteryService(BluetoothService service) async {
  for (final characteristic in service.characteristics) {
    if (characteristic.uuid.toString().toUpperCase().contains(
      BleConstants.batteryLevelCharUuid,
    )) {
      try {
        // 读取当前电量
        final value = await characteristic.read();
        if (value.isNotEmpty) {
          _batteryLevel = value[0]; // 电量值 (0-100)
          notifyListeners();
        }
      } catch (e) {
        debugPrint("Error reading battery: $e");
      }

      // 订阅电量变化通知
      if (characteristic.properties.notify) {
        await characteristic.setNotifyValue(true);
        _batterySubscription = characteristic.lastValueStream.listen((value) {
          if (value.isNotEmpty) {
            _batteryLevel = value[0];
            notifyListeners();
          }
        });
      }
      break;
    }
  }
}
```

### 2.2 电量数据格式
- **数据类型**: 单字节无符号整数 (Uint8)
- **数值范围**: 0-100 (表示电量百分比)
- **更新方式**: 支持主动读取和通知订阅

## 3. 时间同步功能

### 3.1 时间同步协议

```dart
/// 时间同步
Future<void> syncTime() async {
  if (!_isConnected) {
    throw Exception('Device not connected');
  }

  final now = DateTime.now();
  final packet = _createTimeSyncPacket(now);
  await sendData(packet);
  debugPrint('Time synchronized: ${now.hour}:${now.minute}');
}

List<int> _createTimeSyncPacket(DateTime time) {
  final packet = BytesBuilder();
  packet.addByte(BleConstants.cmdTimeSync);  // 命令类型: 0x02
  packet.addByte(time.hour);                 // 小时 (0-23)
  packet.addByte(time.minute);               // 分钟 (0-59)
  packet.addByte(time.second);               // 秒钟 (0-59)
  packet.addByte(time.weekday - 1);          // 星期 (0-6, 0=周一)

  final bytes = packet.toBytes();
  int checksum = 0;
  for (final byte in bytes) {
    checksum += byte.toInt();
  }
  packet.addByte(checksum & 0xFF);           // 校验和

  return packet.toBytes();
}
```

### 3.2 时间同步数据格式

| 字节位置 | 字段 | 长度 | 说明 |
|---------|------|------|------|
| 0 | 命令类型 | 1 | 0x02 (时间同步命令) |
| 1 | 小时 | 1 | 0-23 |
| 2 | 分钟 | 1 | 0-59 |
| 3 | 秒钟 | 1 | 0-59 |
| 4 | 星期 | 1 | 0-6 (0=周一, 6=周日) |
| 5 | 校验和 | 1 | 前面所有字节的和 & 0xFF |

### 3.3 连接成功后自动时间同步

在设备控制页面中，连接成功后会自动触发时间同步：

```dart
@override
void initState() {
  super.initState();
  _blePipeline.addListener(_onBleStateChanged);
  _triggerTimeSync(); // 自动触发时间同步
}

void _triggerTimeSync() {
  if (_blePipeline.isConnected && !_timeSyncCompleted) {
    setState(() {
      _timeSyncInProgress = true;
    });

    Future.delayed(const Duration(seconds: 1), () {
      if (mounted && _blePipeline.isConnected) {
        _sendTimeSync();
      }
    });
  }
}
```

## 4. 消息发送协议

### 4.1 主要数据发送接口

```dart
/// 发送协议消息 - 简化的消息发送
Future<void> sendMessage({
  List<ColorData> colors = const [],
  VibrationType vibration = VibrationType.none,
  ScreenEffect screenEffect = ScreenEffect.none,
  String text = '',
}) async {
  final packet = BleProtocol.createPacket(
    colors: colors,
    vibration: vibration,
    screenEffect: screenEffect,
    text: text,
  );

  await sendData(packet);
}
```

### 4.2 数据发送重试机制

```dart
/// 发送数据 - 统一的数据发送接口
Future<void> sendData(List<int> data) async {
  if (!_isConnected) {
    throw Exception("Device not connected");
  }

  final writeCharacteristic = await _findWriteCharacteristic();
  if (writeCharacteristic == null) {
    throw Exception("Write characteristic not found");
  }

  // 重试机制
  const maxRetries = 3;
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    try {
      await writeCharacteristic.write(data, withoutResponse: true);
      debugPrint('Data sent successfully');
      return;
    } catch (e) {
      if (attempt == maxRetries) {
        throw Exception('Failed to send data after $maxRetries attempts: $e');
      }
      await Future.delayed(Duration(milliseconds: 100 * attempt));
    }
  }
}
```

## 5. 固件实现建议

### 5.1 电量服务实现
1. 实现标准的蓝牙电池服务 (0x180F)
2. 电池电量特征值 (0x2A19) 支持读取和通知
3. 电量值以百分比形式返回 (0-100)

### 5.2 时间同步实现
1. 监听自定义服务 (6E400001-B5A3-F393-E0A9-E50E24DCCA9E) 的写入特征值
2. 解析时间同步命令 (0x02)
3. 验证校验和并更新设备时间
4. 可选择在时间同步成功后发送确认响应

### 5.3 连接稳定性
1. 实现连接状态监听
2. 支持自动重连机制
3. 处理连接超时情况

## 6. 调试建议

1. **电量监控**: 实现电量低警告通知
2. **时间同步确认**: 可选择发送同步成功确认包
3. **错误处理**: 对无效命令或数据格式返回错误响应
4. **日志记录**: 记录关键操作便于调试

---

*本文档基于Flutter应用代码生成，如有更新请及时同步。*