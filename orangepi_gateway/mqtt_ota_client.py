#!/usr/bin/env python3
"""
MQTT OTA客户端
订阅MQTT主题接收固件,通过串口转发给ESP32
"""

import json
import base64
import hashlib
import logging
import os
from pathlib import Path
from typing import Optional, Callable
from threading import Thread
import paho.mqtt.client as mqtt

from uart_ota_sender import UartOTASender

logger = logging.getLogger(__name__)


class MQTTOTAClient:
    """
    MQTT OTA客户端
    负责从MQTT服务器接收固件并通过UART转发给ESP32
    """
    
    def __init__(
        self,
        mqtt_host: str = 'localhost',
        mqtt_port: int = 1883,
        mqtt_username: Optional[str] = None,
        mqtt_password: Optional[str] = None,
        device_id: str = 'esp32_device_001',
        serial_port: str = '/dev/ttyUSB0',
        serial_baudrate: int = 921600,
        firmware_dir: str = '/tmp/ota_firmware'
    ):
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.mqtt_username = mqtt_username
        self.mqtt_password = mqtt_password
        self.device_id = device_id
        self.firmware_dir = Path(firmware_dir)
        
        # MQTT主题
        self.topic_firmware = f"ota/{device_id}/firmware"      # 接收固件
        self.topic_control = f"ota/{device_id}/control"        # 控制命令
        self.topic_status = f"ota/{device_id}/status"          # 状态上报
        self.topic_progress = f"ota/{device_id}/progress"      # 进度上报
        
        # MQTT客户端
        self.mqtt_client = mqtt.Client(client_id=f"gateway_{device_id}")
        self.mqtt_client.on_connect = self._on_connect
        self.mqtt_client.on_disconnect = self._on_disconnect
        self.mqtt_client.on_message = self._on_message
        
        if mqtt_username:
            self.mqtt_client.username_pw_set(mqtt_username, mqtt_password)
        
        # UART发送器
        self.uart_sender = UartOTASender(
            port=serial_port,
            baudrate=serial_baudrate
        )
        self.uart_sender.progress_callback = self._on_ota_progress
        self.uart_sender.complete_callback = self._on_ota_complete
        
        # 固件信息
        self.current_firmware: Optional[dict] = None
        self.ota_thread: Optional[Thread] = None
        
        # 创建固件目录
        self.firmware_dir.mkdir(parents=True, exist_ok=True)
    
    def start(self):
        """启动客户端"""
        logger.info(f"Connecting to MQTT broker: {self.mqtt_host}:{self.mqtt_port}")
        
        # 连接串口
        if not self.uart_sender.connect():
            logger.error("Failed to connect to serial port")
            return False
        
        # 连接MQTT
        try:
            self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, keepalive=60)
            self.mqtt_client.loop_start()
            return True
        except Exception as e:
            logger.error(f"Failed to connect to MQTT: {e}")
            return False
    
    def stop(self):
        """停止客户端"""
        self.mqtt_client.loop_stop()
        self.mqtt_client.disconnect()
        self.uart_sender.disconnect()
        logger.info("Client stopped")
    
    def _on_connect(self, client, userdata, flags, rc):
        """MQTT连接回调"""
        if rc == 0:
            logger.info("Connected to MQTT broker")
            
            # 订阅主题
            client.subscribe(self.topic_firmware, qos=1)
            client.subscribe(self.topic_control, qos=1)
            
            logger.info(f"Subscribed to: {self.topic_firmware}")
            logger.info(f"Subscribed to: {self.topic_control}")
            
            # 发布上线状态
            self._publish_status("online", "Gateway connected")
        else:
            logger.error(f"MQTT connection failed: rc={rc}")
    
    def _on_disconnect(self, client, userdata, rc):
        """MQTT断开回调"""
        logger.warning(f"Disconnected from MQTT: rc={rc}")
    
    def _on_message(self, client, userdata, msg):
        """MQTT消息回调"""
        logger.debug(f"Received message on {msg.topic}")
        
        try:
            if msg.topic == self.topic_firmware:
                self._handle_firmware_message(msg.payload)
            elif msg.topic == self.topic_control:
                self._handle_control_message(msg.payload)
        except Exception as e:
            logger.error(f"Error handling message: {e}")
    
    def _handle_firmware_message(self, payload: bytes):
        """
        处理固件消息
        
        期望的JSON格式:
        {
            "version": "1.0.0",
            "project": "ESP32-Firmware",
            "size": 123456,
            "md5": "abc123...",
            "data": "base64_encoded_firmware_data"
        }
        
        或分块传输:
        {
            "version": "1.0.0",
            "chunk_index": 0,
            "total_chunks": 10,
            "data": "base64_encoded_chunk"
        }
        """
        try:
            msg = json.loads(payload.decode('utf-8'))
        except json.JSONDecodeError:
            # 可能是纯二进制固件
            self._save_and_send_firmware(payload, "unknown", "ESP32-Firmware")
            return
        
        if 'chunk_index' in msg:
            # 分块传输
            self._handle_chunked_firmware(msg)
        else:
            # 完整固件
            version = msg.get('version', '1.0.0')
            project = msg.get('project', 'ESP32-Firmware')
            expected_md5 = msg.get('md5')
            
            if 'data' in msg:
                firmware_data = base64.b64decode(msg['data'])
            else:
                logger.error("No firmware data in message")
                return
            
            # 验证MD5
            if expected_md5:
                actual_md5 = hashlib.md5(firmware_data).hexdigest()
                if actual_md5 != expected_md5:
                    logger.error(f"MD5 mismatch: expected {expected_md5}, got {actual_md5}")
                    self._publish_status("error", "MD5 verification failed")
                    return
            
            self._save_and_send_firmware(firmware_data, version, project)
    
    def _handle_chunked_firmware(self, msg: dict):
        """处理分块固件"""
        chunk_index = msg['chunk_index']
        total_chunks = msg['total_chunks']
        version = msg.get('version', '1.0.0')
        project = msg.get('project', 'ESP32-Firmware')
        
        chunk_data = base64.b64decode(msg['data'])
        
        # 保存分块
        chunk_file = self.firmware_dir / f"chunk_{chunk_index:04d}.bin"
        chunk_file.write_bytes(chunk_data)
        
        logger.info(f"Received chunk {chunk_index + 1}/{total_chunks}")
        
        # 检查是否所有分块都已接收
        if chunk_index == total_chunks - 1:
            # 合并分块
            firmware_data = b''
            for i in range(total_chunks):
                cf = self.firmware_dir / f"chunk_{i:04d}.bin"
                if cf.exists():
                    firmware_data += cf.read_bytes()
                    cf.unlink()  # 删除分块文件
                else:
                    logger.error(f"Missing chunk {i}")
                    self._publish_status("error", f"Missing chunk {i}")
                    return
            
            self._save_and_send_firmware(firmware_data, version, project)
    
    def _save_and_send_firmware(self, firmware_data: bytes, version: str, project: str):
        """保存固件并发送给ESP32"""
        # 保存固件文件
        firmware_path = self.firmware_dir / f"firmware_{version}.bin"
        firmware_path.write_bytes(firmware_data)
        
        logger.info(f"Firmware saved: {firmware_path} ({len(firmware_data)} bytes)")
        
        # 在新线程中发送固件
        self.ota_thread = Thread(
            target=self._send_firmware_thread,
            args=(str(firmware_path), version, project),
            daemon=True
        )
        self.ota_thread.start()
    
    def _send_firmware_thread(self, firmware_path: str, version: str, project: str):
        """发送固件线程"""
        self._publish_status("upgrading", f"Starting OTA: {version}")
        
        success = self.uart_sender.send_firmware(
            firmware_path,
            version=version,
            project_name=project
        )
        
        if success:
            self._publish_status("success", f"OTA completed: {version}")
        else:
            self._publish_status("failed", f"OTA failed: {version}")
    
    def _handle_control_message(self, payload: bytes):
        """
        处理控制命令
        
        支持的命令:
        - {"command": "status"} - 查询状态
        - {"command": "rollback"} - 请求回滚
        - {"command": "abort"} - 中止OTA
        """
        try:
            msg = json.loads(payload.decode('utf-8'))
            command = msg.get('command')
            
            if command == 'status':
                status = self.uart_sender.query_status()
                if status:
                    self._publish_status("info", json.dumps({
                        "state": status.state,
                        "error_code": status.error_code,
                        "received_bytes": status.received_bytes,
                        "total_bytes": status.total_bytes,
                        "version": status.current_version
                    }))
                else:
                    self._publish_status("error", "Failed to query status")
            
            elif command == 'rollback':
                if self.uart_sender.request_rollback():
                    self._publish_status("info", "Rollback requested")
                else:
                    self._publish_status("error", "Rollback failed")
            
            elif command == 'abort':
                # TODO: 实现中止功能
                self._publish_status("info", "Abort not implemented")
            
            else:
                logger.warning(f"Unknown command: {command}")
        
        except json.JSONDecodeError:
            logger.error("Invalid control message")
    
    def _on_ota_progress(self, received: int, total: int, percent: int):
        """OTA进度回调"""
        self.mqtt_client.publish(
            self.topic_progress,
            json.dumps({
                "received": received,
                "total": total,
                "percent": percent
            }),
            qos=0
        )
    
    def _on_ota_complete(self, success: bool, error: int):
        """OTA完成回调"""
        if success:
            self._publish_status("success", "OTA completed, device rebooting")
        else:
            self._publish_status("failed", f"OTA failed with error: {error}")
    
    def _publish_status(self, status: str, message: str):
        """发布状态"""
        self.mqtt_client.publish(
            self.topic_status,
            json.dumps({
                "device_id": self.device_id,
                "status": status,
                "message": message,
                "timestamp": int(__import__('time').time())
            }),
            qos=1
        )
        logger.info(f"Status: {status} - {message}")


# 命令行入口
if __name__ == '__main__':
    import argparse
    import signal
    import sys
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    parser = argparse.ArgumentParser(description='MQTT OTA Client')
    parser.add_argument('--mqtt-host', default='localhost', help='MQTT broker host')
    parser.add_argument('--mqtt-port', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--mqtt-user', help='MQTT username')
    parser.add_argument('--mqtt-pass', help='MQTT password')
    parser.add_argument('--device-id', default='esp32_device_001', help='Device ID')
    parser.add_argument('--serial-port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--serial-baud', type=int, default=921600, help='Baud rate')
    
    args = parser.parse_args()
    
    client = MQTTOTAClient(
        mqtt_host=args.mqtt_host,
        mqtt_port=args.mqtt_port,
        mqtt_username=args.mqtt_user,
        mqtt_password=args.mqtt_pass,
        device_id=args.device_id,
        serial_port=args.serial_port,
        serial_baudrate=args.serial_baud
    )
    
    def signal_handler(sig, frame):
        print("\nShutting down...")
        client.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    if client.start():
        print(f"MQTT OTA Client running for device: {args.device_id}")
        print(f"Firmware topic: ota/{args.device_id}/firmware")
        print(f"Control topic: ota/{args.device_id}/control")
        print("Press Ctrl+C to exit")
        
        # 保持运行
        signal.pause()
    else:
        print("Failed to start client")
        sys.exit(1)
