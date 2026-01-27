#!/usr/bin/env python3
"""
UART OTA发送器
负责通过串口将固件数据发送给ESP32
"""

import serial
import time
import logging
from pathlib import Path
from typing import Optional, Callable
from threading import Thread, Event

from ota_protocol import (
    Command, ErrorCode, State,
    OTAStartPayload, AckPayload, ProgressPayload, StatusPayload,
    build_frame, parse_frame, find_frame_in_buffer,
    calculate_crc32, MAX_PAYLOAD_SIZE, ACK_TIMEOUT_MS, MAX_RETRY_COUNT
)

logger = logging.getLogger(__name__)


class UartOTASender:
    """
    UART OTA固件发送器
    """
    
    def __init__(
        self,
        port: str = '/dev/ttyUSB0',
        baudrate: int = 921600,
        timeout: float = 0.1
    ):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial: Optional[serial.Serial] = None
        
        # 状态
        self.sequence = 0
        self.is_running = False
        self.last_error = ErrorCode.SUCCESS
        
        # 接收缓冲
        self.rx_buffer = b''
        
        # 回调
        self.progress_callback: Optional[Callable[[int, int, int], None]] = None
        self.complete_callback: Optional[Callable[[bool, ErrorCode], None]] = None
        
        # 接收线程
        self._rx_thread: Optional[Thread] = None
        self._stop_event = Event()
        self._response_event = Event()
        self._last_response = None
    
    def connect(self) -> bool:
        """连接串口"""
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            logger.info(f"Connected to {self.port} at {self.baudrate} baud")
            
            # 启动接收线程
            self._stop_event.clear()
            self._rx_thread = Thread(target=self._rx_loop, daemon=True)
            self._rx_thread.start()
            
            return True
        except Exception as e:
            logger.error(f"Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """断开串口"""
        self._stop_event.set()
        if self._rx_thread:
            self._rx_thread.join(timeout=1.0)
        
        if self.serial and self.serial.is_open:
            self.serial.close()
            logger.info("Serial port closed")
    
    def _rx_loop(self):
        """接收线程循环"""
        while not self._stop_event.is_set():
            if self.serial and self.serial.in_waiting:
                data = self.serial.read(self.serial.in_waiting)
                self.rx_buffer += data
                
                # 尝试解析帧
                while True:
                    frame, self.rx_buffer = find_frame_in_buffer(self.rx_buffer)
                    if frame is None:
                        break
                    
                    header, payload, is_valid = parse_frame(frame)
                    if is_valid:
                        self._handle_response(header, payload)
            
            time.sleep(0.001)
    
    def _handle_response(self, header, payload):
        """处理响应帧"""
        cmd = Command(header.command)
        logger.debug(f"Received: {cmd.name}, seq={header.sequence}")
        
        if cmd == Command.OTA_PROGRESS:
            progress = ProgressPayload.unpack(payload)
            logger.info(f"Progress: {progress.percentage}% ({progress.received_bytes}/{progress.total_bytes})")
            if self.progress_callback:
                self.progress_callback(progress.received_bytes, progress.total_bytes, progress.percentage)
        
        elif cmd == Command.OTA_COMPLETE:
            logger.info("OTA completed successfully!")
            if self.complete_callback:
                self.complete_callback(True, ErrorCode.SUCCESS)
        
        elif cmd in (Command.OTA_ACK, Command.OTA_NACK, Command.OTA_READY, 
                     Command.OTA_STATUS_RESP, Command.OTA_ERROR):
            self._last_response = (header, payload)
            self._response_event.set()
    
    def _wait_response(self, timeout_ms: int = ACK_TIMEOUT_MS) -> tuple:
        """等待响应"""
        self._response_event.clear()
        self._last_response = None
        
        if self._response_event.wait(timeout=timeout_ms / 1000.0):
            return self._last_response
        return None, None
    
    def _send_frame(self, command: Command, payload: bytes = b'', offset: int = 0) -> bool:
        """发送帧"""
        frame = build_frame(command, self.sequence, payload, offset)
        try:
            self.serial.write(frame)
            self.serial.flush()
            logger.debug(f"Sent: {command.name}, seq={self.sequence}, len={len(payload)}")
            return True
        except Exception as e:
            logger.error(f"Send failed: {e}")
            return False
    
    def send_firmware(
        self,
        firmware_path: str,
        version: str = "1.0.0",
        project_name: str = "ESP32-Firmware"
    ) -> bool:
        """
        发送固件文件
        
        Args:
            firmware_path: 固件文件路径 (.bin)
            version: 固件版本号
            project_name: 项目名称
        
        Returns:
            是否发送成功
        """
        # 读取固件文件
        firmware_file = Path(firmware_path)
        if not firmware_file.exists():
            logger.error(f"Firmware file not found: {firmware_path}")
            return False
        
        firmware_data = firmware_file.read_bytes()
        firmware_size = len(firmware_data)
        firmware_crc32 = calculate_crc32(firmware_data)
        
        logger.info(f"Firmware: {firmware_path}")
        logger.info(f"Size: {firmware_size} bytes")
        logger.info(f"CRC32: 0x{firmware_crc32:08X}")
        logger.info(f"Version: {version}")
        
        self.sequence = 0
        self.is_running = True
        
        try:
            # 1. 发送OTA_START
            logger.info("Sending OTA_START...")
            start_payload = OTAStartPayload(
                firmware_size=firmware_size,
                firmware_crc32=firmware_crc32,
                version=version,
                project_name=project_name
            )
            
            for retry in range(MAX_RETRY_COUNT):
                if not self._send_frame(Command.OTA_START, start_payload.pack()):
                    continue
                
                header, payload = self._wait_response()
                if header is None:
                    logger.warning(f"OTA_START timeout, retry {retry + 1}/{MAX_RETRY_COUNT}")
                    continue
                
                if Command(header.command) == Command.OTA_READY:
                    logger.info("ESP32 ready to receive firmware")
                    break
                elif Command(header.command) == Command.OTA_NACK:
                    ack = AckPayload.unpack(payload)
                    logger.error(f"OTA_START rejected: {ErrorCode(ack.error_code).name}")
                    return False
            else:
                logger.error("OTA_START failed after retries")
                return False
            
            # 2. 发送数据块
            logger.info("Sending firmware data...")
            offset = 0
            block_size = MAX_PAYLOAD_SIZE
            
            while offset < firmware_size:
                chunk = firmware_data[offset:offset + block_size]
                self.sequence += 1
                
                for retry in range(MAX_RETRY_COUNT):
                    if not self._send_frame(Command.OTA_DATA, chunk, offset):
                        continue
                    
                    header, payload = self._wait_response()
                    if header is None:
                        logger.warning(f"Block {self.sequence} timeout, retry {retry + 1}/{MAX_RETRY_COUNT}")
                        continue
                    
                    if Command(header.command) == Command.OTA_ACK:
                        break
                    elif Command(header.command) == Command.OTA_NACK:
                        ack = AckPayload.unpack(payload)
                        if ErrorCode(ack.error_code) == ErrorCode.ERR_SEQ:
                            # 序列号错误,使用期望的序列号重发
                            self.sequence = ack.expected_seq
                            logger.warning(f"Sequence adjusted to {self.sequence}")
                            continue
                        else:
                            logger.error(f"Block rejected: {ErrorCode(ack.error_code).name}")
                            return False
                else:
                    logger.error(f"Block {self.sequence} failed after retries")
                    return False
                
                offset += len(chunk)
                percent = int((offset * 100) / firmware_size)
                logger.info(f"Sent: {offset}/{firmware_size} bytes ({percent}%)")
            
            # 3. 发送OTA_END
            logger.info("Sending OTA_END...")
            self.sequence += 1
            
            for retry in range(MAX_RETRY_COUNT):
                if not self._send_frame(Command.OTA_END):
                    continue
                
                header, payload = self._wait_response(timeout_ms=10000)  # 校验可能需要更长时间
                if header is None:
                    logger.warning(f"OTA_END timeout, retry {retry + 1}/{MAX_RETRY_COUNT}")
                    continue
                
                if Command(header.command) == Command.OTA_COMPLETE:
                    logger.info("Firmware transfer completed!")
                    return True
                elif Command(header.command) == Command.OTA_NACK:
                    ack = AckPayload.unpack(payload)
                    logger.error(f"OTA_END rejected: {ErrorCode(ack.error_code).name}")
                    return False
            
            logger.error("OTA_END failed after retries")
            return False
            
        except Exception as e:
            logger.error(f"OTA failed: {e}")
            self._send_frame(Command.OTA_ABORT)
            return False
        finally:
            self.is_running = False
    
    def query_status(self) -> Optional[StatusPayload]:
        """查询ESP32 OTA状态"""
        self.sequence += 1
        self._send_frame(Command.OTA_QUERY_STATUS)
        
        header, payload = self._wait_response()
        if header and Command(header.command) == Command.OTA_STATUS_RESP:
            return StatusPayload.unpack(payload)
        return None
    
    def request_rollback(self) -> bool:
        """请求回滚"""
        self.sequence += 1
        self._send_frame(Command.OTA_ROLLBACK_REQ)
        
        header, payload = self._wait_response()
        if header and Command(header.command) == Command.OTA_ACK:
            return True
        return False


# 命令行测试
if __name__ == '__main__':
    import argparse
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    parser = argparse.ArgumentParser(description='UART OTA Sender')
    parser.add_argument('firmware', help='Firmware file path (.bin)')
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('-b', '--baudrate', type=int, default=921600, help='Baud rate')
    parser.add_argument('-v', '--version', default='1.0.0', help='Firmware version')
    parser.add_argument('--status', action='store_true', help='Query status only')
    parser.add_argument('--rollback', action='store_true', help='Request rollback')
    
    args = parser.parse_args()
    
    sender = UartOTASender(port=args.port, baudrate=args.baudrate)
    
    if not sender.connect():
        exit(1)
    
    try:
        if args.status:
            status = sender.query_status()
            if status:
                print(f"State: {State(status.state).name}")
                print(f"Error: {ErrorCode(status.error_code).name}")
                print(f"Progress: {status.received_bytes}/{status.total_bytes}")
                print(f"Version: {status.current_version}")
            else:
                print("Failed to get status")
        
        elif args.rollback:
            if sender.request_rollback():
                print("Rollback requested successfully")
            else:
                print("Rollback request failed")
        
        else:
            success = sender.send_firmware(
                args.firmware,
                version=args.version
            )
            if success:
                print("OTA completed successfully!")
            else:
                print("OTA failed!")
                exit(1)
    
    finally:
        sender.disconnect()
