#!/usr/bin/env python3
"""
OTA协议定义
与ESP32端的OTAProtocol.hpp保持一致
"""

import struct
from enum import IntEnum
from dataclasses import dataclass
from typing import Optional
import binascii

# ============== 协议常量 ==============

FRAME_HEADER_1 = 0xAA
FRAME_HEADER_2 = 0x55
FRAME_FOOTER_1 = 0x55
FRAME_FOOTER_2 = 0xAA
PROTOCOL_VERSION = 0x01

MAX_PAYLOAD_SIZE = 1024
FRAME_HEADER_SIZE = 14
FRAME_FOOTER_SIZE = 4
MAX_FRAME_SIZE = FRAME_HEADER_SIZE + MAX_PAYLOAD_SIZE + FRAME_FOOTER_SIZE

ACK_TIMEOUT_MS = 3000
BLOCK_TIMEOUT_MS = 5000
MAX_RETRY_COUNT = 3


# ============== 命令类型 ==============

class Command(IntEnum):
    # 主机 -> ESP32
    OTA_START = 0x01
    OTA_DATA = 0x02
    OTA_END = 0x03
    OTA_ABORT = 0x04
    OTA_QUERY_STATUS = 0x05
    OTA_ROLLBACK_REQ = 0x06
    
    # ESP32 -> 主机
    OTA_ACK = 0x80
    OTA_NACK = 0x81
    OTA_READY = 0x82
    OTA_PROGRESS = 0x83
    OTA_COMPLETE = 0x84
    OTA_ERROR = 0x85
    OTA_STATUS_RESP = 0x86


class ErrorCode(IntEnum):
    SUCCESS = 0x00
    ERR_CRC = 0x01
    ERR_SEQ = 0x02
    ERR_OFFSET = 0x03
    ERR_FLASH_WRITE = 0x04
    ERR_FLASH_ERASE = 0x05
    ERR_PARTITION = 0x06
    ERR_IMAGE_INVALID = 0x07
    ERR_VERSION = 0x08
    ERR_NO_MEMORY = 0x09
    ERR_TIMEOUT = 0x0A
    ERR_BUSY = 0x0B
    ERR_INVALID_STATE = 0x0C
    ERR_FRAME_INVALID = 0x0D
    ERR_ROLLBACK_FAILED = 0x0E
    ERR_UNKNOWN = 0xFF


class State(IntEnum):
    IDLE = 0x00
    WAITING_START = 0x01
    RECEIVING = 0x02
    VERIFYING = 0x03
    APPLYING = 0x04
    COMPLETED = 0x05
    ERROR = 0x06
    ROLLBACK = 0x07


# ============== 数据结构 ==============

@dataclass
class FrameHeader:
    """帧头结构"""
    header1: int = FRAME_HEADER_1
    header2: int = FRAME_HEADER_2
    version: int = PROTOCOL_VERSION
    command: int = 0
    sequence: int = 0
    offset: int = 0
    length: int = 0
    
    FORMAT = '<BBBBHIH'  # 小端: 2B header, 1B version, 1B cmd, 2B seq, 4B offset, 2B len
    SIZE = 14
    
    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.header1, self.header2, self.version, self.command,
            self.sequence, self.offset, self.length
        )
    
    @classmethod
    def unpack(cls, data: bytes) -> 'FrameHeader':
        if len(data) < cls.SIZE:
            raise ValueError(f"Data too short: {len(data)} < {cls.SIZE}")
        values = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            header1=values[0], header2=values[1], version=values[2],
            command=values[3], sequence=values[4], offset=values[5],
            length=values[6]
        )


@dataclass
class FrameFooter:
    """帧尾结构"""
    crc16: int = 0
    footer1: int = FRAME_FOOTER_1
    footer2: int = FRAME_FOOTER_2
    
    FORMAT = '<HBB'
    SIZE = 4
    
    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.crc16, self.footer1, self.footer2)
    
    @classmethod
    def unpack(cls, data: bytes) -> 'FrameFooter':
        if len(data) < cls.SIZE:
            raise ValueError(f"Data too short: {len(data)} < {cls.SIZE}")
        values = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(crc16=values[0], footer1=values[1], footer2=values[2])


@dataclass
class OTAStartPayload:
    """OTA开始命令Payload"""
    firmware_size: int
    firmware_crc32: int
    version: str
    project_name: str
    block_size: int = MAX_PAYLOAD_SIZE
    
    FORMAT = '<II32s32sI'
    SIZE = 76
    
    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.firmware_size, self.firmware_crc32,
            self.version.encode('utf-8')[:32].ljust(32, b'\x00'),
            self.project_name.encode('utf-8')[:32].ljust(32, b'\x00'),
            self.block_size
        )


@dataclass
class AckPayload:
    """ACK/NACK Payload"""
    error_code: int
    expected_seq: int
    received_bytes: int
    
    FORMAT = '<BHI'
    SIZE = 7
    
    @classmethod
    def unpack(cls, data: bytes) -> 'AckPayload':
        if len(data) < cls.SIZE:
            raise ValueError(f"Data too short: {len(data)} < {cls.SIZE}")
        values = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(error_code=values[0], expected_seq=values[1], received_bytes=values[2])


@dataclass
class ProgressPayload:
    """进度报告Payload"""
    received_bytes: int
    total_bytes: int
    percentage: int
    
    FORMAT = '<IIB'
    SIZE = 9
    
    @classmethod
    def unpack(cls, data: bytes) -> 'ProgressPayload':
        if len(data) < cls.SIZE:
            raise ValueError(f"Data too short: {len(data)} < {cls.SIZE}")
        values = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(received_bytes=values[0], total_bytes=values[1], percentage=values[2])


@dataclass
class StatusPayload:
    """状态响应Payload"""
    state: int
    error_code: int
    received_bytes: int
    total_bytes: int
    current_version: str
    
    FORMAT = '<BBII32s'
    SIZE = 42
    
    @classmethod
    def unpack(cls, data: bytes) -> 'StatusPayload':
        if len(data) < cls.SIZE:
            raise ValueError(f"Data too short: {len(data)} < {cls.SIZE}")
        values = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        version = values[4].rstrip(b'\x00').decode('utf-8', errors='ignore')
        return cls(
            state=values[0], error_code=values[1],
            received_bytes=values[2], total_bytes=values[3],
            current_version=version
        )


# ============== CRC计算 ==============

def calculate_crc16(data: bytes) -> int:
    """计算CRC16-CCITT"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def calculate_crc32(data: bytes) -> int:
    """计算CRC32"""
    return binascii.crc32(data) & 0xFFFFFFFF


# ============== 帧构建和解析 ==============

def build_frame(command: Command, sequence: int, payload: bytes = b'', offset: int = 0) -> bytes:
    """
    构建完整的OTA帧
    """
    header = FrameHeader(
        command=command,
        sequence=sequence,
        offset=offset,
        length=len(payload)
    )
    
    # 构建帧头 + Payload
    frame_data = header.pack() + payload
    
    # 计算CRC
    crc = calculate_crc16(frame_data)
    
    # 构建帧尾
    footer = FrameFooter(crc16=crc)
    
    return frame_data + footer.pack()


def parse_frame(data: bytes) -> tuple:
    """
    解析OTA帧
    返回: (header, payload, is_valid)
    """
    if len(data) < FRAME_HEADER_SIZE + FRAME_FOOTER_SIZE:
        return None, None, False
    
    # 解析帧头
    header = FrameHeader.unpack(data)
    
    # 验证帧头标识
    if header.header1 != FRAME_HEADER_1 or header.header2 != FRAME_HEADER_2:
        return None, None, False
    
    # 计算预期帧长度
    expected_len = FRAME_HEADER_SIZE + header.length + FRAME_FOOTER_SIZE
    if len(data) < expected_len:
        return header, None, False
    
    # 提取Payload
    payload = data[FRAME_HEADER_SIZE:FRAME_HEADER_SIZE + header.length]
    
    # 解析帧尾
    footer_start = FRAME_HEADER_SIZE + header.length
    footer = FrameFooter.unpack(data[footer_start:])
    
    # 验证帧尾标识
    if footer.footer1 != FRAME_FOOTER_1 or footer.footer2 != FRAME_FOOTER_2:
        return header, payload, False
    
    # 验证CRC
    crc_data = data[:footer_start]
    expected_crc = calculate_crc16(crc_data)
    if footer.crc16 != expected_crc:
        return header, payload, False
    
    return header, payload, True


def find_frame_in_buffer(buffer: bytes) -> tuple:
    """
    在缓冲区中查找完整帧
    返回: (frame_bytes, remaining_buffer) 或 (None, buffer)
    """
    # 查找帧头
    for i in range(len(buffer) - 1):
        if buffer[i] == FRAME_HEADER_1 and buffer[i + 1] == FRAME_HEADER_2:
            # 检查是否有足够的数据读取帧头
            if i + FRAME_HEADER_SIZE <= len(buffer):
                header = FrameHeader.unpack(buffer[i:])
                expected_len = FRAME_HEADER_SIZE + header.length + FRAME_FOOTER_SIZE
                
                if i + expected_len <= len(buffer):
                    # 完整帧
                    frame = buffer[i:i + expected_len]
                    remaining = buffer[i + expected_len:]
                    return frame, remaining
                else:
                    # 帧不完整,保留数据
                    return None, buffer[i:]
    
    # 没有找到帧头,清空缓冲区(保留最后一个字节以防帧头跨包)
    if len(buffer) > 0:
        return None, buffer[-1:] if buffer[-1] == FRAME_HEADER_1 else b''
    return None, b''
