#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MQTT OTA 客户端

在 OrangePi 上运行，负责：
1. 订阅云端 MQTT 主题，接收固件更新通知
2. 从云端下载固件到本地
3. 启动 HTTP 服务器供 ESP32 下载
4. 通过 MQTT 通知 ESP32 进行 OTA 升级

OTA 链路: 云端(MQTT通知) -> OrangePi(4G下载) -> HTTP服务器 -> ESP32(WiFi HTTP下载)
"""

import os
import sys
import json
import hashlib
import logging
import requests
import threading
import time
from pathlib import Path
from typing import Optional, Callable
import paho.mqtt.client as mqtt

from ota_http_server import OTAHttpServer, FirmwareInfo

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('MQTTOTAClient')


class MQTTOTAClient:
    """
    MQTT OTA 客户端
    
    负责从云端接收固件更新通知，下载固件，并通知 ESP32 进行升级
    """
    
    def __init__(
        self,
        mqtt_host: str = 'localhost',
        mqtt_port: int = 1883,
        mqtt_username: Optional[str] = None,
        mqtt_password: Optional[str] = None,
        device_id: str = 'esp32_device_001',
        gateway_ip: str = '192.168.4.1',
        http_port: int = 8000,
        firmware_dir: str = '/opt/ota_firmware'
    ):
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.mqtt_username = mqtt_username
        self.mqtt_password = mqtt_password
        self.device_id = device_id
        self.gateway_ip = gateway_ip
        self.http_port = http_port
        self.firmware_dir = Path(firmware_dir)
        
        # MQTT 主题
        # 云端 -> 网关
        self.topic_firmware_notify = f"ota/{device_id}/firmware/notify"   # 固件更新通知
        self.topic_control = f"ota/{device_id}/control"                    # 控制命令
        
        # 网关 -> 云端
        self.topic_status = f"ota/{device_id}/status"                      # 状态上报
        self.topic_progress = f"ota/{device_id}/progress"                  # 进度上报
        
        # 网关 -> ESP32
        self.topic_esp32_upgrade = f"esp32/{device_id}/ota/upgrade"        # 通知 ESP32 升级
        
        # ESP32 -> 网关
        self.topic_esp32_status = f"esp32/{device_id}/ota/status"          # ESP32 OTA 状态
        
        # MQTT 客户端
        self.mqtt_client = mqtt.Client(client_id=f"gateway_{device_id}")
        self.mqtt_client.on_connect = self._on_connect
        self.mqtt_client.on_disconnect = self._on_disconnect
        self.mqtt_client.on_message = self._on_message
        
        if mqtt_username:
            self.mqtt_client.username_pw_set(mqtt_username, mqtt_password)
        
        # HTTP 服务器
        self.http_server = OTAHttpServer(
            host='0.0.0.0',
            port=http_port,
            firmware_dir=str(firmware_dir)
        )
        
        # 设置 HTTP 服务器回调
        self.http_server.set_callbacks(
            on_download_start=self._on_firmware_download_start,
            on_download_complete=self._on_firmware_download_complete
        )
        
        # 当前固件信息
        self.current_firmware: Optional[FirmwareInfo] = None
        self.download_thread: Optional[threading.Thread] = None
        
        # 创建固件目录
        self.firmware_dir.mkdir(parents=True, exist_ok=True)
    
    def start(self) -> bool:
        """启动客户端"""
        logger.info("=" * 50)
        logger.info("Starting MQTT OTA Client")
        logger.info("=" * 50)
        
        # 启动 HTTP 服务器（后台模式）
        try:
            self.http_server.start(background=True)
            logger.info(f"HTTP server started on port {self.http_port}")
        except Exception as e:
            logger.error(f"Failed to start HTTP server: {e}")
            return False
        
        # 连接 MQTT
        try:
            logger.info(f"Connecting to MQTT broker: {self.mqtt_host}:{self.mqtt_port}")
            self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, keepalive=60)
            self.mqtt_client.loop_start()
            return True
        except Exception as e:
            logger.error(f"Failed to connect to MQTT: {e}")
            self.http_server.stop()
            return False
    
    def stop(self):
        """停止客户端"""
        logger.info("Stopping MQTT OTA Client...")
        self.mqtt_client.loop_stop()
        self.mqtt_client.disconnect()
        self.http_server.stop()
        logger.info("Client stopped")
    
    def _on_connect(self, client, userdata, flags, rc):
        """MQTT 连接回调"""
        if rc == 0:
            logger.info("Connected to MQTT broker")
            
            # 订阅主题
            client.subscribe(self.topic_firmware_notify, qos=1)
            client.subscribe(self.topic_control, qos=1)
            client.subscribe(self.topic_esp32_status, qos=1)
            
            logger.info(f"Subscribed: {self.topic_firmware_notify}")
            logger.info(f"Subscribed: {self.topic_control}")
            logger.info(f"Subscribed: {self.topic_esp32_status}")
            
            # 发布上线状态
            self._publish_status("online", "Gateway connected and ready")
        else:
            logger.error(f"MQTT connection failed: rc={rc}")
    
    def _on_disconnect(self, client, userdata, rc):
        """MQTT 断开回调"""
        logger.warning(f"Disconnected from MQTT: rc={rc}")
        if rc != 0:
            logger.info("Attempting to reconnect...")
    
    def _on_message(self, client, userdata, msg):
        """MQTT 消息回调"""
        logger.info(f"Received message on topic: {msg.topic}")
        
        try:
            if msg.topic == self.topic_firmware_notify:
                self._handle_firmware_notify(msg.payload)
            elif msg.topic == self.topic_control:
                self._handle_control_message(msg.payload)
            elif msg.topic == self.topic_esp32_status:
                self._handle_esp32_status(msg.payload)
        except Exception as e:
            logger.error(f"Error handling message: {e}", exc_info=True)
    
    def _handle_firmware_notify(self, payload: bytes):
        """
        处理固件更新通知
        
        期望的 JSON 格式:
        {
            "version": "1.2.0",
            "download_url": "https://cloud.example.com/firmware/v1.2.0/firmware.bin",
            "md5": "abc123def456...",
            "size": 1234567,
            "release_notes": "Bug fixes and improvements"
        }
        """
        try:
            msg = json.loads(payload.decode('utf-8'))
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in firmware notify: {e}")
            return
        
        version = msg.get('version', 'unknown')
        download_url = msg.get('download_url')
        expected_md5 = msg.get('md5')
        expected_size = msg.get('size', 0)
        release_notes = msg.get('release_notes', '')
        
        if not download_url:
            logger.error("No download_url in firmware notify")
            self._publish_status("error", "Missing download URL")
            return
        
        logger.info(f"Firmware update notification received:")
        logger.info(f"  Version: {version}")
        logger.info(f"  URL: {download_url}")
        logger.info(f"  Size: {expected_size} bytes")
        logger.info(f"  MD5: {expected_md5}")
        
        # 在后台线程下载固件
        self.download_thread = threading.Thread(
            target=self._download_firmware,
            args=(download_url, version, expected_md5, expected_size)
        )
        self.download_thread.start()
    
    def _download_firmware(self, url: str, version: str, expected_md5: Optional[str], 
                           expected_size: int):
        """下载固件（在后台线程运行）"""
        self._publish_status("downloading", f"Downloading firmware v{version}")
        
        try:
            # 固件文件名
            filename = f"firmware_v{version}.bin"
            file_path = self.firmware_dir / filename
            
            logger.info(f"Downloading firmware from: {url}")
            
            # 下载固件
            response = requests.get(url, stream=True, timeout=300)
            response.raise_for_status()
            
            # 获取文件大小
            total_size = int(response.headers.get('content-length', expected_size))
            
            # 写入文件
            downloaded = 0
            md5_hash = hashlib.md5()
            last_progress = 0
            
            with open(file_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    if chunk:
                        f.write(chunk)
                        md5_hash.update(chunk)
                        downloaded += len(chunk)
                        
                        # 报告进度
                        if total_size > 0:
                            progress = int(downloaded * 100 / total_size)
                            if progress >= last_progress + 10:
                                last_progress = (progress // 10) * 10
                                logger.info(f"Download progress: {progress}%")
                                self._publish_progress("download", progress)
            
            # 验证 MD5
            actual_md5 = md5_hash.hexdigest()
            if expected_md5 and actual_md5 != expected_md5:
                logger.error(f"MD5 mismatch! Expected: {expected_md5}, Got: {actual_md5}")
                self._publish_status("error", "MD5 verification failed")
                file_path.unlink()  # 删除损坏的文件
                return
            
            logger.info(f"Firmware downloaded successfully: {filename}")
            logger.info(f"  Size: {downloaded} bytes")
            logger.info(f"  MD5: {actual_md5}")
            
            # 更新当前固件信息
            self.current_firmware = FirmwareInfo(str(file_path))
            
            # 发布状态
            self._publish_status("ready", f"Firmware v{version} ready for OTA")
            
            # 通知 ESP32 进行升级
            self._notify_esp32_upgrade(filename, version, actual_md5)
            
        except requests.RequestException as e:
            logger.error(f"Failed to download firmware: {e}")
            self._publish_status("error", f"Download failed: {str(e)}")
        except Exception as e:
            logger.error(f"Error during firmware download: {e}", exc_info=True)
            self._publish_status("error", f"Error: {str(e)}")
    
    def _notify_esp32_upgrade(self, filename: str, version: str, md5: str):
        """通知 ESP32 进行 OTA 升级"""
        # 构建固件下载 URL
        firmware_url = f"http://{self.gateway_ip}:{self.http_port}/{filename}"
        
        upgrade_msg = {
            "action": "upgrade",
            "url": firmware_url,
            "version": version,
            "md5": md5,
            "timestamp": int(time.time())
        }
        
        logger.info(f"Notifying ESP32 to upgrade from: {firmware_url}")
        
        self.mqtt_client.publish(
            self.topic_esp32_upgrade,
            json.dumps(upgrade_msg),
            qos=1
        )
    
    def _handle_control_message(self, payload: bytes):
        """处理控制命令"""
        try:
            msg = json.loads(payload.decode('utf-8'))
        except json.JSONDecodeError:
            logger.error("Invalid JSON in control message")
            return
        
        command = msg.get('command', '')
        
        if command == 'status':
            # 返回当前状态
            self._publish_status("ok", "System running normally")
            
        elif command == 'list_firmware':
            # 列出可用固件
            firmware_list = self.http_server.list_firmware()
            response = {
                "command": "list_firmware",
                "firmware": [f.to_dict() for f in firmware_list]
            }
            self.mqtt_client.publish(self.topic_status, json.dumps(response), qos=1)
            
        elif command == 'trigger_upgrade':
            # 手动触发升级
            filename = msg.get('filename')
            if filename:
                file_path = self.firmware_dir / filename
                if file_path.exists():
                    info = FirmwareInfo(str(file_path))
                    self._notify_esp32_upgrade(filename, info.version, info.md5_hash)
                else:
                    self._publish_status("error", f"Firmware not found: {filename}")
            else:
                # 使用最新固件
                firmware_list = self.http_server.list_firmware()
                if firmware_list:
                    latest = firmware_list[-1]
                    self._notify_esp32_upgrade(latest.file_name, latest.version, latest.md5_hash)
                else:
                    self._publish_status("error", "No firmware available")
                    
        elif command == 'clean_firmware':
            # 清理旧固件
            keep = msg.get('keep', 2)  # 保留最新的N个
            firmware_list = sorted(self.http_server.list_firmware(), 
                                   key=lambda x: x.file_path.stat().st_mtime)
            removed = 0
            while len(firmware_list) > keep:
                old = firmware_list.pop(0)
                self.http_server.remove_firmware(old.file_name)
                removed += 1
            self._publish_status("ok", f"Removed {removed} old firmware files")
        
        else:
            logger.warning(f"Unknown command: {command}")
    
    def _handle_esp32_status(self, payload: bytes):
        """处理 ESP32 OTA 状态上报"""
        try:
            msg = json.loads(payload.decode('utf-8'))
        except json.JSONDecodeError:
            logger.error("Invalid JSON in ESP32 status")
            return
        
        status = msg.get('status', 'unknown')
        progress = msg.get('progress', 0)
        message = msg.get('message', '')
        
        logger.info(f"ESP32 OTA Status: {status} ({progress}%) - {message}")
        
        # 转发给云端
        forward_msg = {
            "device_id": self.device_id,
            "esp32_status": status,
            "progress": progress,
            "message": message,
            "timestamp": int(time.time())
        }
        self.mqtt_client.publish(f"cloud/{self.device_id}/ota/status", 
                                 json.dumps(forward_msg), qos=1)
    
    def _on_firmware_download_start(self, client: str, filename: str):
        """HTTP 服务器回调：固件下载开始"""
        logger.info(f"ESP32 started downloading: {filename} from {client}")
        self._publish_progress("esp32_download", 0)
    
    def _on_firmware_download_complete(self, client: str, filename: str, success: bool):
        """HTTP 服务器回调：固件下载完成"""
        status = "success" if success else "failed"
        logger.info(f"ESP32 download {status}: {filename} from {client}")
        
        if success:
            self._publish_status("esp32_downloading", "ESP32 received firmware")
    
    def _publish_status(self, status: str, message: str):
        """发布状态消息"""
        msg = {
            "status": status,
            "message": message,
            "device_id": self.device_id,
            "timestamp": int(time.time())
        }
        self.mqtt_client.publish(self.topic_status, json.dumps(msg), qos=1)
        logger.info(f"Status published: {status} - {message}")
    
    def _publish_progress(self, phase: str, progress: int):
        """发布进度消息"""
        msg = {
            "phase": phase,
            "progress": progress,
            "device_id": self.device_id,
            "timestamp": int(time.time())
        }
        self.mqtt_client.publish(self.topic_progress, json.dumps(msg), qos=0)


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='MQTT OTA Client for OrangePi Gateway')
    parser.add_argument('--mqtt-host', default='localhost', help='MQTT broker host')
    parser.add_argument('--mqtt-port', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--mqtt-user', help='MQTT username')
    parser.add_argument('--mqtt-pass', help='MQTT password')
    parser.add_argument('--device-id', default='esp32_device_001', help='Device ID')
    parser.add_argument('--gateway-ip', default='192.168.4.1', 
                        help='Gateway IP (ESP32 will connect to this)')
    parser.add_argument('--http-port', type=int, default=8000, help='HTTP server port')
    parser.add_argument('--firmware-dir', default='/opt/ota_firmware', help='Firmware directory')
    
    args = parser.parse_args()
    
    client = MQTTOTAClient(
        mqtt_host=args.mqtt_host,
        mqtt_port=args.mqtt_port,
        mqtt_username=args.mqtt_user,
        mqtt_password=args.mqtt_pass,
        device_id=args.device_id,
        gateway_ip=args.gateway_ip,
        http_port=args.http_port,
        firmware_dir=args.firmware_dir
    )
    
    if client.start():
        logger.info("MQTT OTA Client is running...")
        logger.info("Press Ctrl+C to stop")
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Received Ctrl+C, shutting down...")
            client.stop()
    else:
        logger.error("Failed to start client")
        sys.exit(1)


if __name__ == '__main__':
    main()
