/**
 * @file OTAProtocol.hpp
 * @brief UART OTA 协议定义
 * 
 * 协议帧格式：
 * ┌─────┬─────┬─────┬──────┬──────┬────────┬──────────┬───────┬─────┐
 * │帧头  │版本  │命令  │序列号 │偏移量  │数据长度  │ Payload  │ CRC16 │帧尾  │
 * │2B   │1B   │1B   │2B    │4B     │2B      │ 0~1024B  │  2B   │2B   │
 * └─────┴─────┴─────┴──────┴──────┴────────┴──────────┴───────┴─────┘
 * 
 * 总帧长度 = 16字节(固定头部) + Payload长度
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace OTA {

// ============== 协议常量定义 ==============

// 帧标识
constexpr uint8_t FRAME_HEADER_1     = 0xAA;
constexpr uint8_t FRAME_HEADER_2     = 0x55;
constexpr uint8_t FRAME_FOOTER_1     = 0x55;
constexpr uint8_t FRAME_FOOTER_2     = 0xAA;

// 协议版本
constexpr uint8_t PROTOCOL_VERSION   = 0x01;

// 数据块大小
constexpr size_t  MAX_PAYLOAD_SIZE   = 1024;
constexpr size_t  FRAME_HEADER_SIZE  = 14;    // 帧头到Payload前的固定部分
constexpr size_t  FRAME_FOOTER_SIZE  = 4;     // CRC16 + 帧尾
constexpr size_t  MAX_FRAME_SIZE     = FRAME_HEADER_SIZE + MAX_PAYLOAD_SIZE + FRAME_FOOTER_SIZE;

// 超时设置 (毫秒)
constexpr uint32_t ACK_TIMEOUT_MS    = 3000;  // ACK超时时间
constexpr uint32_t BLOCK_TIMEOUT_MS  = 5000;  // 数据块超时时间
constexpr uint8_t  MAX_RETRY_COUNT   = 3;     // 最大重传次数

// ============== 命令类型定义 ==============
enum class Command : uint8_t {
    // ---- 主机 -> ESP32 ----
    OTA_START           = 0x01,   // 开始升级：携带固件总大小、版本信息
    OTA_DATA            = 0x02,   // 数据块传输
    OTA_END             = 0x03,   // 传输完成
    OTA_ABORT           = 0x04,   // 中止升级
    OTA_QUERY_STATUS    = 0x05,   // 查询升级状态
    OTA_ROLLBACK_REQ    = 0x06,   // 请求回滚
    
    // ---- ESP32 -> 主机 ----
    OTA_ACK             = 0x80,   // 通用确认
    OTA_NACK            = 0x81,   // 拒绝/错误
    OTA_READY           = 0x82,   // 准备就绪
    OTA_PROGRESS        = 0x83,   // 进度报告
    OTA_COMPLETE        = 0x84,   // 升级完成
    OTA_ERROR           = 0x85,   // 错误报告
    OTA_STATUS_RESP     = 0x86,   // 状态响应
};

// ============== 错误码定义 ==============
enum class ErrorCode : uint8_t {
    SUCCESS              = 0x00,   // 成功
    ERR_CRC              = 0x01,   // CRC校验失败
    ERR_SEQ              = 0x02,   // 序列号错误
    ERR_OFFSET           = 0x03,   // 偏移量错误
    ERR_FLASH_WRITE      = 0x04,   // Flash写入失败
    ERR_FLASH_ERASE      = 0x05,   // Flash擦除失败
    ERR_PARTITION        = 0x06,   // 分区错误
    ERR_IMAGE_INVALID    = 0x07,   // 镜像校验失败
    ERR_VERSION          = 0x08,   // 版本检查失败
    ERR_NO_MEMORY        = 0x09,   // 内存不足
    ERR_TIMEOUT          = 0x0A,   // 超时
    ERR_BUSY             = 0x0B,   // 设备忙
    ERR_INVALID_STATE    = 0x0C,   // 状态错误
    ERR_FRAME_INVALID    = 0x0D,   // 帧格式错误
    ERR_ROLLBACK_FAILED  = 0x0E,   // 回滚失败
    ERR_UNKNOWN          = 0xFF,   // 未知错误
};

// ============== OTA状态机 ==============
enum class State : uint8_t {
    IDLE                 = 0x00,   // 空闲状态
    WAITING_START        = 0x01,   // 等待开始命令
    RECEIVING            = 0x02,   // 接收数据中
    VERIFYING            = 0x03,   // 校验中
    APPLYING             = 0x04,   // 应用更新中
    COMPLETED            = 0x05,   // 完成
    ERROR                = 0x06,   // 错误状态
    ROLLBACK             = 0x07,   // 回滚中
};

// ============== 帧结构定义 ==============

#pragma pack(push, 1)

/**
 * @brief 通用帧头结构
 */
struct FrameHeader {
    uint8_t  header1;      // 0xAA
    uint8_t  header2;      // 0x55
    uint8_t  version;      // 协议版本
    uint8_t  command;      // 命令类型
    uint16_t sequence;     // 序列号
    uint32_t offset;       // 偏移量（用于数据块）
    uint16_t length;       // Payload长度
};

/**
 * @brief 帧尾结构
 */
struct FrameFooter {
    uint16_t crc16;        // CRC16校验
    uint8_t  footer1;      // 0x55
    uint8_t  footer2;      // 0xAA
};

/**
 * @brief OTA开始命令 Payload
 */
struct OTAStartPayload {
    uint32_t firmwareSize;          // 固件总大小
    uint32_t firmwareCRC32;         // 固件整体CRC32
    uint8_t  version[32];           // 版本字符串
    uint8_t  projectName[32];       // 项目名称
    uint32_t blockSize;             // 推荐的数据块大小
};

/**
 * @brief ACK/NACK Payload
 */
struct AckPayload {
    uint8_t  errorCode;             // 错误码
    uint16_t expectedSeq;           // 期望的下一个序列号
    uint32_t receivedBytes;         // 已接收字节数
};

/**
 * @brief 进度报告 Payload
 */
struct ProgressPayload {
    uint32_t receivedBytes;         // 已接收字节数
    uint32_t totalBytes;            // 总字节数
    uint8_t  percentage;            // 百分比
};

/**
 * @brief 状态响应 Payload
 */
struct StatusPayload {
    uint8_t  state;                 // 当前状态
    uint8_t  errorCode;             // 最后的错误码
    uint32_t receivedBytes;         // 已接收字节数
    uint32_t totalBytes;            // 总字节数
    uint8_t  currentVersion[32];    // 当前运行版本
};

/**
 * @brief 完整的OTA帧 (用于发送)
 */
struct OTAFrame {
    FrameHeader header;
    uint8_t     payload[MAX_PAYLOAD_SIZE];
    FrameFooter footer;
    
    // 获取完整帧大小
    size_t getTotalSize() const {
        return sizeof(FrameHeader) + header.length + sizeof(FrameFooter);
    }
};

#pragma pack(pop)

// ============== CRC16计算 (CCITT) ==============

/**
 * @brief 计算CRC16-CCITT
 * @param data 数据指针
 * @param length 数据长度
 * @return CRC16值
 */
inline uint16_t calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 计算CRC32 (用于固件整体校验)
 */
inline uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// ============== 帧构建辅助函数 ==============

/**
 * @brief 构建帧头
 */
inline void buildFrameHeader(FrameHeader& header, Command cmd, uint16_t seq, 
                             uint32_t offset = 0, uint16_t payloadLen = 0) {
    header.header1  = FRAME_HEADER_1;
    header.header2  = FRAME_HEADER_2;
    header.version  = PROTOCOL_VERSION;
    header.command  = static_cast<uint8_t>(cmd);
    header.sequence = seq;
    header.offset   = offset;
    header.length   = payloadLen;
}

/**
 * @brief 构建帧尾 (包含CRC计算)
 * @param footer 帧尾结构
 * @param frameData 从帧头开始的数据
 * @param frameDataLen 数据长度(不含CRC和帧尾)
 */
inline void buildFrameFooter(FrameFooter& footer, const uint8_t* frameData, size_t frameDataLen) {
    footer.crc16   = calculateCRC16(frameData, frameDataLen);
    footer.footer1 = FRAME_FOOTER_1;
    footer.footer2 = FRAME_FOOTER_2;
}

/**
 * @brief 验证帧CRC
 */
inline bool verifyFrameCRC(const uint8_t* frameData, size_t totalLen) {
    if (totalLen < sizeof(FrameHeader) + sizeof(FrameFooter)) {
        return false;
    }
    size_t crcDataLen = totalLen - sizeof(FrameFooter);
    uint16_t expectedCRC = calculateCRC16(frameData, crcDataLen);
    uint16_t receivedCRC = *reinterpret_cast<const uint16_t*>(frameData + crcDataLen);
    return expectedCRC == receivedCRC;
}

/**
 * @brief 验证帧头和帧尾标识
 */
inline bool verifyFrameMarkers(const uint8_t* frameData, size_t totalLen) {
    if (totalLen < sizeof(FrameHeader) + sizeof(FrameFooter)) {
        return false;
    }
    // 检查帧头
    if (frameData[0] != FRAME_HEADER_1 || frameData[1] != FRAME_HEADER_2) {
        return false;
    }
    // 检查帧尾
    if (frameData[totalLen - 2] != FRAME_FOOTER_1 || frameData[totalLen - 1] != FRAME_FOOTER_2) {
        return false;
    }
    return true;
}

} // namespace OTA
