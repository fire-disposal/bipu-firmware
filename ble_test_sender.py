#!/usr/bin/env python3
"""
BLE消息发送测试脚本
用于测试BIPI设备的BLE消息接收功能
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

# BLE配置
DEVICE_NAME = "BIPI_PAGER"
SERVICE_UUID = "00001234-0000-0000-0000-000000000000"
CHAR_UUID = "00005678-0000-0000-0000-000000000000"

def format_message(sender, message):
    """格式化消息：sender|message"""
    return f"{sender}|{message}"

async def find_device():
    """扫描并找到BIPI设备"""
    print(f"正在扫描BLE设备，查找'{DEVICE_NAME}'...")
    
    devices = await BleakScanner.discover()
    for device in devices:
        if device.name and DEVICE_NAME in device.name:
            print(f"找到设备: {device.name} - {device.address}")
            return device.address
    
    print(f"未找到设备'{DEVICE_NAME}'")
    return None

async def send_message(device_address, sender, message):
    """发送消息到BIPI设备"""
    try:
        async with BleakClient(device_address) as client:
            print(f"已连接到设备: {device_address}")
            
            # 检查服务是否可用
            services = await client.get_services()
            service_found = False
            char_found = False
            
            for service in services:
                if service.uuid == SERVICE_UUID:
                    service_found = True
                    for char in service.characteristics:
                        if char.uuid == CHAR_UUID:
                            char_found = True
                            break
                    break
            
            if not service_found:
                print(f"未找到服务: {SERVICE_UUID}")
                return False
                
            if not char_found:
                print(f"未找到特征: {CHAR_UUID}")
                return False
            
            # 格式化并发送消息
            formatted_message = format_message(sender, message)
            message_bytes = formatted_message.encode('utf-8')
            
            print(f"发送消息: {formatted_message}")
            await client.write_gatt_char(CHAR_UUID, message_bytes)
            print("消息发送成功！")
            return True
            
    except BleakError as e:
        print(f"BLE错误: {e}")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False

async def main():
    """主函数"""
    if len(sys.argv) < 3:
        print("使用方法: python ble_test_sender.py <发送者> <消息内容>")
        print("示例: python ble_test_sender.py 手机 '这是一条测试消息'")
        return
    
    sender = sys.argv[1]
    message = " ".join(sys.argv[2:])
    
    # 查找设备
    device_address = await find_device()
    if not device_address:
        return
    
    # 发送消息
    success = await send_message(device_address, sender, message)
    if success:
        print("测试完成！")
    else:
        print("测试失败！")

if __name__ == "__main__":
    asyncio.run(main())