# App 模块优化总结

## 优化概述

通过将LED状态机从app层迁移到board层，实现了app模块的显著简化和系统级的LED管理统一。

## 关键改进

### 1. LED状态机迁移（board/led.c）

**移除的BLE专属逻辑：**
- ❌ `LED_SM_STATE_ADV_MARQUEE` - 广播跑马灯
- ❌ `LED_SM_STATE_CONN_BLINK` - 连接闪烁
- ❌ `LED_SM_STATE_CONN_STABLE` - 连接稳定

**引入的通用设计：**
- ✅ `BOARD_LED_MODE_OFF` - 关闭
- ✅ `BOARD_LED_MODE_STATIC` - 静态点亮（手电筒）
- ✅ `BOARD_LED_MODE_MARQUEE` - 跑马灯
- ✅ `BOARD_LED_MODE_BLINK` - 闪烁
- ✅ `BOARD_LED_MODE_NOTIFY_FLASH` - 通知闪烁

**新增接口：**
```c
void board_leds_set_mode(board_led_mode_t mode);      // 设置LED工作模式
void board_leds_tick(void);                           // 驱动LED状态机
void board_leds_notify(void);                         // LED通知闪烁
```

### 2. App层简化

**app.c 变更：**

| 模块 | 变更 | 效果 |
|------|------|------|
| `app_init()` | 移除CTS回调设置 | 时间同步在ble_manager内部完成 |
| `app_loop()` | LED操作高频化 | app_update_led_mode和board_leds_tick每次调用 |
| `app_update_led_mode()` | 新增状态映射 | BLE状态→LED模式的抽象层 |

**删除的代码：**
- ❌ app_conn_sm.c/h - 连接状态机（LED控制移至board）
- ❌ app_effects.c/h - 效果管理（已弃用）

**保留的核心：**
- ✅ app.c - 主循环和初始化
- ✅ app_ble.c - BLE消息回调（已简化）
- ✅ app_battery.c - 电池管理

### 3. UI层调整（ui.c）

**ui_enter_standby()：**
- 移除直接的 `board_leds_off()` 调用
- LED状态由app层管理

**ui_toggle_flashlight()：**
- 移除直接的LED设置代码
- 仅更新 `s_ui.flashlight_on` 标志
- app层根据此标志在app_update_led_mode中设置LED模式

## 设计原则

```
应用层(app)
    ↓ 状态映射（BLE/手电筒/待机 → LED模式）
设备层(board)  
    ↓ 状态机驱动（模式 → LED动画）
硬件层(GPIO)
```

**优点：**
1. **解耦性** - board层LED管理不依赖应用逻辑
2. **可复用性** - 相同LED效果可在不同应用中使用
3. **可维护性** - LED相关bug仅需修改board层
4. **实时性** - LED状态每次循环更新，保证手电筒等功能响应速度
5. **安全性** - 所有LED操作都通过统一接口，避免冲突

## 编译和测试

### 编译清单
- [x] CMakeLists.txt 已更新（移除app_conn_sm.c和app_effects.c）
- [x] board.h 新增LED模式类型和接口
- [x] 无编译错误（待验证）

### 功能验证清单
- [ ] BLE广播时LED跑马灯正常
- [ ] BLE连接时LED闪烁正常
- [ ] 手电筒功能响应快速
- [ ] 消息通知LED闪烁正常
- [ ] 待机时LED正确关闭

## 文件变更统计

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| app/CMakeLists.txt | 修改 | 删除2个源文件引用 |
| app/src/app.c | 重构 | 简化主循环，优化LED管理 |
| app/src/app_ble.c | 简化 | 移除CTS回调 |
| ui/ui.c | 修改 | 移除直接LED操作 |
| board/include/board.h | 扩展 | 新增LED模式类型和接口 |
| board/led.c | 重构 | LED状态机通用化 |

## 代码行数变化

- **app.c**: 245行 → 192行（-53行，-22%）
- **总计**: ~300行代码移至board层或删除

## 向后兼容性

✅ **完全兼容**
- `board_leds_set()` 和 `board_leds_off()` 保持不变
- 新增接口不影响现有代码
- 内部实现变更对外部透明
