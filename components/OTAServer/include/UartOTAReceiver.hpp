/**
 * @file UartOTAReceiver.hpp
 * @brief ESP32-S3 UART OTA 接收器
 * 
 * 功能：
 * - 通过UART接收固件数据
 * - 使用Ring Buffer缓冲接收数据
 * - 调用ESP-IDF OTA API写入Flash
 * - 支持回滚机制
 * 
 * 基于 native_ota_example 改造，任务绑定到 Core 1
 */

#pragma once

#include "Thread.hpp"
#include "OTAProtocol.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include <cstring>
#include <functional>

class UartOTAReceiver : public Thread {
public:
    // 回调函数类型
    using ProgressCallback = std::function<void(uint32_t received, uint32_t total, uint8_t percent)>;
    using CompleteCallback = std::function<void(bool success, OTA::ErrorCode error)>;
    using StateChangeCallback = std::function<void(OTA::State oldState, OTA::State newState)>;

    /**
     * @brief 构造函数
     * @param uartNum UART端口号 (UART_NUM_1 或 UART_NUM_2)
     * @param txPin TX引脚
     * @param rxPin RX引脚
     * @param baudRate 波特率 (推荐 921600)
     */
    UartOTAReceiver(uart_port_t uartNum = UART_NUM_1, 
                    int txPin = 17, 
                    int rxPin = 18,
                    int baudRate = 921600)
        : Thread("UartOTA", 1024 * 16, tskIDLE_PRIORITY + 5, 1)  // 绑定到 Core 1
        , m_uartNum(uartNum)
        , m_txPin(txPin)
        , m_rxPin(rxPin)
        , m_baudRate(baudRate)
    {
    }

    ~UartOTAReceiver() override {
        deinit();
    }

    /**
     * @brief 初始化UART和Ring Buffer
     */
    esp_err_t init();

    /**
     * @brief 反初始化
     */
    void deinit();

    /**
     * @brief 获取当前状态
     */
    OTA::State getState() const { return m_state; }

    /**
     * @brief 获取最后的错误码
     */
    OTA::ErrorCode getLastError() const { return m_lastError; }

    /**
     * @brief 获取接收进度
     */
    void getProgress(uint32_t& received, uint32_t& total) const {
        received = m_receivedBytes;
        total = m_totalBytes;
    }

    /**
     * @brief 设置进度回调
     */
    void setProgressCallback(ProgressCallback cb) { m_progressCb = cb; }

    /**
     * @brief 设置完成回调
     */
    void setCompleteCallback(CompleteCallback cb) { m_completeCb = cb; }

    /**
     * @brief 设置状态变化回调
     */
    void setStateChangeCallback(StateChangeCallback cb) { m_stateChangeCb = cb; }

    /**
     * @brief 启动首次自检（应在app_main中调用）
     * @return true 表示应用有效，false 表示需要回滚
     */
    static bool performDiagnostic();

    /**
     * @brief 标记应用有效，取消回滚
     */
    static void markAppValid();

    /**
     * @brief 标记应用无效并回滚重启
     */
    static void markAppInvalidAndRollback();

protected:
    void run() override;

private:
    static constexpr const char* TAG = "UartOTA";
    
    // UART配置
    uart_port_t m_uartNum;
    int m_txPin;
    int m_rxPin;
    int m_baudRate;
    
    // Ring Buffer
    RingbufHandle_t m_ringBuf = nullptr;
    static constexpr size_t RING_BUF_SIZE = 1024 * 32;  // 32KB Ring Buffer
    
    // 帧接收缓冲
    uint8_t m_frameBuffer[OTA::MAX_FRAME_SIZE];
    size_t m_frameBufferPos = 0;
    
    // OTA状态
    OTA::State m_state = OTA::State::IDLE;
    OTA::ErrorCode m_lastError = OTA::ErrorCode::SUCCESS;
    
    // OTA句柄和分区
    esp_ota_handle_t m_otaHandle = 0;
    const esp_partition_t* m_updatePartition = nullptr;
    
    // 固件信息
    uint32_t m_totalBytes = 0;
    uint32_t m_receivedBytes = 0;
    uint32_t m_firmwareCRC32 = 0;
    uint16_t m_expectedSeq = 0;
    bool m_imageHeaderChecked = false;
    
    // 回调
    ProgressCallback m_progressCb;
    CompleteCallback m_completeCb;
    StateChangeCallback m_stateChangeCb;
    
    // 内部方法
    void setState(OTA::State newState);
    void setError(OTA::ErrorCode error);
    
    // 帧处理
    bool receiveFrame(uint8_t* buffer, size_t& length, uint32_t timeoutMs);
    bool parseAndValidateFrame(const uint8_t* data, size_t length, OTA::FrameHeader& header);
    void processFrame(const OTA::FrameHeader& header, const uint8_t* payload);
    
    // 命令处理
    void handleOTAStart(const OTA::FrameHeader& header, const uint8_t* payload);
    void handleOTAData(const OTA::FrameHeader& header, const uint8_t* payload);
    void handleOTAEnd(const OTA::FrameHeader& header);
    void handleOTAAbort(const OTA::FrameHeader& header);
    void handleOTAQueryStatus(const OTA::FrameHeader& header);
    void handleOTARollback(const OTA::FrameHeader& header);
    
    // 响应发送
    void sendAck(uint16_t seq, OTA::ErrorCode error = OTA::ErrorCode::SUCCESS);
    void sendNack(uint16_t seq, OTA::ErrorCode error);
    void sendReady(uint16_t seq);
    void sendProgress(uint16_t seq);
    void sendComplete(uint16_t seq);
    void sendStatus(uint16_t seq);
    void sendFrame(OTA::Command cmd, uint16_t seq, const uint8_t* payload, uint16_t payloadLen);
    
    // OTA操作
    esp_err_t beginOTA();
    esp_err_t writeOTAData(const uint8_t* data, size_t length);
    esp_err_t endOTA();
    void abortOTA();
    
    // 镜像校验
    bool checkImageHeader(const uint8_t* data, size_t length);
};
