#!/usr/bin/env python3
"""
固件上传工具
将编译好的固件上传到MQTT服务器
"""
import json
import base64
import hashlib
import argparse
import paho.mqtt.client as mqtt
from pathlib import Path


def upload_firmware(
    broker: str,
    port: int,
    device_id: str,
    firmware_path: str,
    version: str,
    project: str = "HybridLink",
    username: str = None,
    password: str = None,
    chunk_size: int = 0
):
    """
    上传固件到MQTT服务器
    
    Args:
        broker: MQTT服务器地址
        port: MQTT端口
        device_id: 设备ID
        firmware_path: 固件文件路径
        version: 固件版本号
        project: 项目名称
        username: MQTT用户名
        password: MQTT密码
        chunk_size: 分块大小(0=不分块)
    """
    # 读取固件
    firmware_file = Path(firmware_path)
    if not firmware_file.exists():
        print(f"Error: Firmware file not found: {firmware_path}")
        return False
    
    firmware = firmware_file.read_bytes()
    firmware_size = len(firmware)
    
    # 计算MD5
    md5 = hashlib.md5(firmware).hexdigest()
    
    print(f"Firmware: {firmware_path}")
    print(f"Size: {firmware_size} bytes ({firmware_size / 1024:.1f} KB)")
    print(f"MD5: {md5}")
    print(f"Version: {version}")
    print(f"Target device: {device_id}")
    print()
    
    # 连接MQTT
    client = mqtt.Client(client_id=f"firmware_uploader_{device_id}")
    
    if username:
        client.username_pw_set(username, password)
    
    try:
        client.connect(broker, port, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"Error: Failed to connect to MQTT broker: {e}")
        return False
    
    topic = f"ota/{device_id}/firmware"
    
    try:
        if chunk_size > 0 and firmware_size > chunk_size:
            # 分块传输
            total_chunks = (firmware_size + chunk_size - 1) // chunk_size
            print(f"Sending in {total_chunks} chunks ({chunk_size} bytes each)...")
            
            for i in range(total_chunks):
                start = i * chunk_size
                end = min(start + chunk_size, firmware_size)
                chunk_data = firmware[start:end]
                
                message = {
                    "version": version,
                    "project": project,
                    "chunk_index": i,
                    "total_chunks": total_chunks,
                    "data": base64.b64encode(chunk_data).decode('ascii')
                }
                
                result = client.publish(topic, json.dumps(message), qos=1)
                result.wait_for_publish()
                
                percent = int(((i + 1) * 100) / total_chunks)
                print(f"Sent chunk {i + 1}/{total_chunks} ({percent}%)")
            
            print("All chunks sent!")
        else:
            # 整体传输
            print("Sending firmware as single message...")
            
            message = {
                "version": version,
                "project": project,
                "size": firmware_size,
                "md5": md5,
                "data": base64.b64encode(firmware).decode('ascii')
            }
            
            result = client.publish(topic, json.dumps(message), qos=1)
            result.wait_for_publish()
            
            print("Firmware sent!")
        
        print(f"\nPublished to topic: {topic}")
        return True
        
    except Exception as e:
        print(f"Error: Failed to publish: {e}")
        return False
    
    finally:
        client.loop_stop()
        client.disconnect()


def send_control_command(
    broker: str,
    port: int,
    device_id: str,
    command: str,
    username: str = None,
    password: str = None
):
    """发送控制命令"""
    client = mqtt.Client()
    
    if username:
        client.username_pw_set(username, password)
    
    try:
        client.connect(broker, port)
        
        topic = f"ota/{device_id}/control"
        message = {"command": command}
        
        client.publish(topic, json.dumps(message), qos=1)
        print(f"Sent command '{command}' to {topic}")
        
        client.disconnect()
        return True
        
    except Exception as e:
        print(f"Error: {e}")
        return False


def subscribe_status(
    broker: str,
    port: int,
    device_id: str,
    username: str = None,
    password: str = None
):
    """订阅设备状态"""
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            topics = [
                f"ota/{device_id}/status",
                f"ota/{device_id}/progress"
            ]
            for topic in topics:
                client.subscribe(topic)
                print(f"Subscribed to: {topic}")
        else:
            print(f"Connection failed: rc={rc}")
    
    def on_message(client, userdata, msg):
        try:
            data = json.loads(msg.payload.decode('utf-8'))
            print(f"[{msg.topic}] {json.dumps(data, indent=2)}")
        except:
            print(f"[{msg.topic}] {msg.payload}")
    
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    if username:
        client.username_pw_set(username, password)
    
    try:
        client.connect(broker, port)
        print(f"Connected to {broker}:{port}")
        print("Listening for status updates... (Ctrl+C to exit)")
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nExiting...")
    except Exception as e:
        print(f"Error: {e}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Firmware Upload Tool')
    subparsers = parser.add_subparsers(dest='action', help='Action to perform')
    
    # 上传命令
    upload_parser = subparsers.add_parser('upload', help='Upload firmware')
    upload_parser.add_argument('firmware', help='Firmware file path (.bin)')
    upload_parser.add_argument('-v', '--version', default='1.0.0', help='Firmware version')
    upload_parser.add_argument('-p', '--project', default='HybridLink', help='Project name')
    upload_parser.add_argument('--chunk-size', type=int, default=0, help='Chunk size (0=no chunking)')
    
    # 控制命令
    control_parser = subparsers.add_parser('control', help='Send control command')
    control_parser.add_argument('command', choices=['status', 'rollback', 'abort'], help='Command')
    
    # 监控命令
    monitor_parser = subparsers.add_parser('monitor', help='Monitor device status')
    
    # 公共参数
    for p in [upload_parser, control_parser, monitor_parser]:
        p.add_argument('--broker', default='localhost', help='MQTT broker host')
        p.add_argument('--port', type=int, default=1883, help='MQTT broker port')
        p.add_argument('--device-id', default='esp32_device_001', help='Device ID')
        p.add_argument('--username', help='MQTT username')
        p.add_argument('--password', help='MQTT password')
    
    args = parser.parse_args()
    
    if args.action == 'upload':
        upload_firmware(
            broker=args.broker,
            port=args.port,
            device_id=args.device_id,
            firmware_path=args.firmware,
            version=args.version,
            project=args.project,
            username=args.username,
            password=args.password,
            chunk_size=args.chunk_size
        )
    
    elif args.action == 'control':
        send_control_command(
            broker=args.broker,
            port=args.port,
            device_id=args.device_id,
            command=args.command,
            username=args.username,
            password=args.password
        )
    
    elif args.action == 'monitor':
        subscribe_status(
            broker=args.broker,
            port=args.port,
            device_id=args.device_id,
            username=args.username,
            password=args.password
        )
    
    else:
        parser.print_help()
