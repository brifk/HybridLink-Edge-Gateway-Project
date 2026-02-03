#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA HTTP 固件服务器

在 OrangePi 上运行，为 ESP32 提供 HTTP 固件下载服务。
OTA 链路: 云端 -> OrangePi(4G下载) -> HTTP服务器 -> ESP32(WiFi HTTP下载)
"""

import os
import sys
import json
import hashlib
import logging
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from typing import Optional, Callable

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('OTAHttpServer')


class FirmwareInfo:
    """固件信息类"""
    def __init__(self, file_path: str):
        self.file_path = Path(file_path)
        self.file_name = self.file_path.name
        self.file_size = 0
        self.md5_hash = ""
        self.version = ""
        
        if self.file_path.exists():
            self._compute_info()
    
    def _compute_info(self):
        """计算固件信息"""
        self.file_size = self.file_path.stat().st_size
        
        # 计算 MD5
        md5 = hashlib.md5()
        with open(self.file_path, 'rb') as f:
            for chunk in iter(lambda: f.read(8192), b''):
                md5.update(chunk)
        self.md5_hash = md5.hexdigest()
        
        # 尝试从文件名提取版本号
        name = self.file_path.stem  # 去掉扩展名
        # 假设格式: firmware_v1.0.0.bin
        if '_v' in name:
            self.version = name.split('_v')[-1]
        else:
            self.version = "unknown"
    
    def to_dict(self) -> dict:
        return {
            "file_name": self.file_name,
            "file_size": self.file_size,
            "md5": self.md5_hash,
            "version": self.version
        }


class OTARequestHandler(SimpleHTTPRequestHandler):
    """OTA HTTP 请求处理器"""
    
    firmware_dir: Path = Path('/opt/ota_firmware')
    on_download_start: Optional[Callable] = None
    on_download_complete: Optional[Callable] = None
    
    def __init__(self, *args, **kwargs):
        # 设置工作目录为固件目录
        super().__init__(*args, directory=str(self.firmware_dir), **kwargs)
    
    def do_GET(self):
        """处理 GET 请求"""
        logger.info(f"GET request: {self.path} from {self.client_address}")
        
        if self.path == '/info' or self.path == '/info.json':
            self._send_firmware_info()
        elif self.path == '/health':
            self._send_health_check()
        elif self.path.endswith('.bin'):
            self._serve_firmware()
        else:
            super().do_GET()
    
    def _send_firmware_info(self):
        """发送固件信息 JSON"""
        firmware_list = []
        
        for file_path in self.firmware_dir.glob('*.bin'):
            info = FirmwareInfo(str(file_path))
            firmware_list.append(info.to_dict())
        
        response = {
            "status": "ok",
            "firmware_count": len(firmware_list),
            "firmware_list": firmware_list
        }
        
        self._send_json_response(response)
    
    def _send_health_check(self):
        """健康检查端点"""
        response = {
            "status": "ok",
            "service": "OTA HTTP Server",
            "firmware_dir": str(self.firmware_dir)
        }
        self._send_json_response(response)
    
    def _send_json_response(self, data: dict):
        """发送 JSON 响应"""
        response_bytes = json.dumps(data, indent=2).encode('utf-8')
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', len(response_bytes))
        self.end_headers()
        self.wfile.write(response_bytes)
    
    def _serve_firmware(self):
        """提供固件下载"""
        # 获取文件名
        file_name = self.path.lstrip('/')
        file_path = self.firmware_dir / file_name
        
        if not file_path.exists():
            self.send_error(404, f"Firmware not found: {file_name}")
            return
        
        file_size = file_path.stat().st_size
        logger.info(f"Serving firmware: {file_name} ({file_size} bytes)")
        
        # 回调通知下载开始
        if self.on_download_start:
            self.on_download_start(str(self.client_address), file_name)
        
        try:
            with open(file_path, 'rb') as f:
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', file_size)
                self.send_header('Content-Disposition', f'attachment; filename="{file_name}"')
                self.end_headers()
                
                # 分块发送
                sent = 0
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    sent += len(chunk)
            
            logger.info(f"Firmware sent successfully: {file_name} ({sent} bytes)")
            
            # 回调通知下载完成
            if self.on_download_complete:
                self.on_download_complete(str(self.client_address), file_name, True)
                
        except Exception as e:
            logger.error(f"Error serving firmware: {e}")
            if self.on_download_complete:
                self.on_download_complete(str(self.client_address), file_name, False)


class OTAHttpServer:
    """OTA HTTP 服务器"""
    
    def __init__(self, host: str = '0.0.0.0', port: int = 8000, 
                 firmware_dir: str = '/opt/ota_firmware'):
        self.host = host
        self.port = port
        self.firmware_dir = Path(firmware_dir)
        self.server: Optional[HTTPServer] = None
        self.server_thread: Optional[threading.Thread] = None
        
        # 确保固件目录存在
        self.firmware_dir.mkdir(parents=True, exist_ok=True)
        
        # 设置处理器的固件目录
        OTARequestHandler.firmware_dir = self.firmware_dir
    
    def set_callbacks(self, on_download_start: Optional[Callable] = None,
                      on_download_complete: Optional[Callable] = None):
        """设置回调函数"""
        OTARequestHandler.on_download_start = on_download_start
        OTARequestHandler.on_download_complete = on_download_complete
    
    def start(self, background: bool = False):
        """启动服务器"""
        try:
            self.server = HTTPServer((self.host, self.port), OTARequestHandler)
            logger.info(f"OTA HTTP Server starting on http://{self.host}:{self.port}")
            logger.info(f"Firmware directory: {self.firmware_dir}")
            
            # 列出可用固件
            firmware_files = list(self.firmware_dir.glob('*.bin'))
            if firmware_files:
                logger.info(f"Available firmware files: {len(firmware_files)}")
                for f in firmware_files:
                    info = FirmwareInfo(str(f))
                    logger.info(f"  - {info.file_name}: {info.file_size} bytes, MD5: {info.md5_hash[:16]}...")
            else:
                logger.warning("No firmware files found in directory")
            
            if background:
                self.server_thread = threading.Thread(target=self.server.serve_forever)
                self.server_thread.daemon = True
                self.server_thread.start()
                logger.info("Server started in background")
            else:
                logger.info("Server running (Ctrl+C to stop)...")
                self.server.serve_forever()
                
        except Exception as e:
            logger.error(f"Failed to start server: {e}")
            raise
    
    def stop(self):
        """停止服务器"""
        if self.server:
            logger.info("Stopping OTA HTTP Server...")
            self.server.shutdown()
            self.server = None
    
    def add_firmware(self, source_path: str, target_name: Optional[str] = None) -> FirmwareInfo:
        """添加固件文件到服务目录"""
        source = Path(source_path)
        if not source.exists():
            raise FileNotFoundError(f"Source file not found: {source_path}")
        
        target_name = target_name or source.name
        target_path = self.firmware_dir / target_name
        
        # 复制文件
        import shutil
        shutil.copy2(source, target_path)
        
        info = FirmwareInfo(str(target_path))
        logger.info(f"Added firmware: {info.file_name} ({info.file_size} bytes)")
        
        return info
    
    def remove_firmware(self, file_name: str) -> bool:
        """删除固件文件"""
        file_path = self.firmware_dir / file_name
        if file_path.exists():
            file_path.unlink()
            logger.info(f"Removed firmware: {file_name}")
            return True
        return False
    
    def list_firmware(self) -> list:
        """列出所有固件"""
        firmware_list = []
        for file_path in self.firmware_dir.glob('*.bin'):
            info = FirmwareInfo(str(file_path))
            firmware_list.append(info)
        return firmware_list


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='OTA HTTP Firmware Server')
    parser.add_argument('--host', default='0.0.0.0', help='Server host (default: 0.0.0.0)')
    parser.add_argument('--port', type=int, default=8000, help='Server port (default: 8000)')
    parser.add_argument('--firmware-dir', default='/opt/ota_firmware', help='Firmware directory')
    
    args = parser.parse_args()
    
    # 创建并启动服务器
    server = OTAHttpServer(
        host=args.host,
        port=args.port,
        firmware_dir=args.firmware_dir
    )
    
    # 设置回调
    def on_download_start(client, filename):
        logger.info(f"[CALLBACK] Download started: {filename} by {client}")
    
    def on_download_complete(client, filename, success):
        status = "SUCCESS" if success else "FAILED"
        logger.info(f"[CALLBACK] Download {status}: {filename} by {client}")
    
    server.set_callbacks(on_download_start, on_download_complete)
    
    try:
        server.start(background=False)
    except KeyboardInterrupt:
        logger.info("Received Ctrl+C, shutting down...")
        server.stop()


if __name__ == '__main__':
    main()
