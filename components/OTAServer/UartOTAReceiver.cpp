/**
 * @file UartOTAReceiver.cpp
 * @brief ESP32-S3 UART OTA 接收器实现
 */

#include "UartOTAReceiver.hpp"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_system.h"
#include <algorithm>

// ============== 初始化/反初始化 ==============

esp_err_t UartOTAReceiver::init() {
    ESP_LOGI(TAG, "Initializing UART OTA Receiver...");
    
    // UART配置
    uart_config_t uart_config = {
        .baud_rate = m_baudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {0},
    };
    
    esp_err_t ret = uart_param_config(m_uartNum, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(m_uartNum, m_txPin, m_rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 安装UART驱动，使用Ring Buffer
    const int uart_buffer_size = 1024 * 4;
    ret = uart_driver_install(m_uartNum, uart_buffer_size, uart_buffer_size, 0, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建Ring Buffer用于帧缓冲
    m_ringBuf = xRingbufferCreate(RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (m_ringBuf == nullptr) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        uart_driver_delete(m_uartNum);
        return ESP_ERR_NO_MEM;
    }
    
    setState(OTA::State::IDLE);
    ESP_LOGI(TAG, "UART OTA Receiver initialized on UART%d (TX:%d, RX:%d, Baud:%d)",
             m_uartNum, m_txPin, m_rxPin, m_baudRate);
    
    return ESP_OK;
}

void UartOTAReceiver::deinit() {
    if (m_ringBuf) {
        vRingbufferDelete(m_ringBuf);
        m_ringBuf = nullptr;
    }
    uart_driver_delete(m_uartNum);
    ESP_LOGI(TAG, "UART OTA Receiver deinitialized");
}

// ============== 诊断和回滚 ==============

bool UartOTAReceiver::performDiagnostic() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Running pending verify image, performing diagnostic...");
            
            // 在这里执行自检逻辑
            // 例如：检查关键外设是否正常、检查配置是否有效等
            vTaskDelay(pdMS_TO_TICKS(2000));  // 模拟自检耗时
            
            // 这里可以添加实际的诊断逻辑
            // 例如：检查传感器、网络连接、配置文件等
            bool diagnosticPassed = true;  // 假设诊断通过
            
            return diagnosticPassed;
        }
    }
    
    return true;  // 非OTA启动，默认通过
}

void UartOTAReceiver::markAppValid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "App marked as valid, rollback cancelled");
    } else {
        ESP_LOGW(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
    }
}

void UartOTAReceiver::markAppInvalidAndRollback() {
    ESP_LOGE(TAG, "App marked as invalid, rolling back...");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // 此函数不会返回
}

// ============== 状态管理 ==============

void UartOTAReceiver::setState(OTA::State newState) {
    if (m_state != newState) {
        OTA::State oldState = m_state;
        m_state = newState;
        
        ESP_LOGI(TAG, "State changed: %d -> %d", 
                 static_cast<int>(oldState), static_cast<int>(newState));
        
        if (m_stateChangeCb) {
            m_stateChangeCb(oldState, newState);
        }
    }
}

void UartOTAReceiver::setError(OTA::ErrorCode error) {
    m_lastError = error;
    if (error != OTA::ErrorCode::SUCCESS) {
        setState(OTA::State::ERROR);
        ESP_LOGE(TAG, "Error occurred: 0x%02X", static_cast<int>(error));
    }
}

// ============== 主任务循环 ==============

void UartOTAReceiver::run() {
    ESP_LOGI(TAG, "UART OTA task started on Core %d", xPortGetCoreID());
    
    uint8_t rxBuffer[256];
    size_t rxLen;
    
    while (true) {
        // 从UART读取数据
        int len = uart_read_bytes(m_uartNum, rxBuffer, sizeof(rxBuffer), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            // 写入Ring Buffer
            if (xRingbufferSend(m_ringBuf, rxBuffer, len, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropping %d bytes", len);
            }
        }
        
        // 尝试接收完整帧
        size_t frameLen = 0;
        if (receiveFrame(m_frameBuffer, frameLen, 10)) {
            OTA::FrameHeader header;
            if (parseAndValidateFrame(m_frameBuffer, frameLen, header)) {
                const uint8_t* payload = m_frameBuffer + sizeof(OTA::FrameHeader);
                processFrame(header, payload);
            }
        }
        
        // 处理超时
        if (m_state == OTA::State::RECEIVING) {
            // 可以在这里添加接收超时处理逻辑
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ============== 帧接收和解析 ==============

bool UartOTAReceiver::receiveFrame(uint8_t* buffer, size_t& length, uint32_t timeoutMs) {
    size_t itemSize;
    uint8_t* item = (uint8_t*)xRingbufferReceive(m_ringBuf, &itemSize, pdMS_TO_TICKS(timeoutMs));
    
    if (item == nullptr) {
        return false;
    }
    
    // 在缓冲区中搜索帧头
    for (size_t i = 0; i < itemSize - 1; i++) {
        if (item[i] == OTA::FRAME_HEADER_1 && item[i + 1] == OTA::FRAME_HEADER_2) {
            // 找到帧头，检查是否有足够的数据读取帧头结构
            if (i + sizeof(OTA::FrameHeader) <= itemSize) {
                OTA::FrameHeader* header = reinterpret_cast<OTA::FrameHeader*>(&item[i]);
                size_t expectedFrameLen = sizeof(OTA::FrameHeader) + header->length + sizeof(OTA::FrameFooter);
                
                if (i + expectedFrameLen <= itemSize) {
                    // 完整帧
                    memcpy(buffer, &item[i], expectedFrameLen);
                    length = expectedFrameLen;
                    
                    // 将剩余数据放回Ring Buffer
                    size_t remaining = itemSize - (i + expectedFrameLen);
                    if (remaining > 0) {
                        xRingbufferSend(m_ringBuf, &item[i + expectedFrameLen], remaining, 0);
                    }
                    
                    vRingbufferReturnItem(m_ringBuf, item);
                    return true;
                }
            }
        }
    }
    
    // 没有找到完整帧，放回Ring Buffer
    vRingbufferReturnItem(m_ringBuf, item);
    return false;
}

bool UartOTAReceiver::parseAndValidateFrame(const uint8_t* data, size_t length, OTA::FrameHeader& header) {
    // 验证帧标识
    if (!OTA::verifyFrameMarkers(data, length)) {
        ESP_LOGW(TAG, "Invalid frame markers");
        return false;
    }
    
    // 验证CRC
    if (!OTA::verifyFrameCRC(data, length)) {
        ESP_LOGW(TAG, "CRC verification failed");
        return false;
    }
    
    // 解析帧头
    memcpy(&header, data, sizeof(OTA::FrameHeader));
    
    // 验证协议版本
    if (header.version != OTA::PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Unsupported protocol version: %d", header.version);
        return false;
    }
    
    return true;
}

void UartOTAReceiver::processFrame(const OTA::FrameHeader& header, const uint8_t* payload) {
    OTA::Command cmd = static_cast<OTA::Command>(header.command);
    
    ESP_LOGD(TAG, "Received command: 0x%02X, seq: %d, len: %d", 
             header.command, header.sequence, header.length);
    
    switch (cmd) {
        case OTA::Command::OTA_START:
            handleOTAStart(header, payload);
            break;
            
        case OTA::Command::OTA_DATA:
            handleOTAData(header, payload);
            break;
            
        case OTA::Command::OTA_END:
            handleOTAEnd(header);
            break;
            
        case OTA::Command::OTA_ABORT:
            handleOTAAbort(header);
            break;
            
        case OTA::Command::OTA_QUERY_STATUS:
            handleOTAQueryStatus(header);
            break;
            
        case OTA::Command::OTA_ROLLBACK_REQ:
            handleOTARollback(header);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", header.command);
            sendNack(header.sequence, OTA::ErrorCode::ERR_FRAME_INVALID);
            break;
    }
}

// ============== 命令处理 ==============

void UartOTAReceiver::handleOTAStart(const OTA::FrameHeader& header, const uint8_t* payload) {
    ESP_LOGI(TAG, "Received OTA_START command");
    
    if (m_state != OTA::State::IDLE && m_state != OTA::State::ERROR) {
        ESP_LOGW(TAG, "Invalid state for OTA_START: %d", static_cast<int>(m_state));
        sendNack(header.sequence, OTA::ErrorCode::ERR_INVALID_STATE);
        return;
    }
    
    // 解析开始命令Payload
    if (header.length < sizeof(OTA::OTAStartPayload)) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_FRAME_INVALID);
        return;
    }
    
    const OTA::OTAStartPayload* startPayload = reinterpret_cast<const OTA::OTAStartPayload*>(payload);
    
    m_totalBytes = startPayload->firmwareSize;
    m_firmwareCRC32 = startPayload->firmwareCRC32;
    m_receivedBytes = 0;
    m_expectedSeq = 0;
    m_imageHeaderChecked = false;
    
    ESP_LOGI(TAG, "Firmware size: %lu bytes", m_totalBytes);
    ESP_LOGI(TAG, "Firmware CRC32: 0x%08lX", m_firmwareCRC32);
    ESP_LOGI(TAG, "Version: %s", startPayload->version);
    ESP_LOGI(TAG, "Project: %s", startPayload->projectName);
    
    // 开始OTA
    esp_err_t err = beginOTA();
    if (err != ESP_OK) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_PARTITION);
        return;
    }
    
    setState(OTA::State::RECEIVING);
    sendReady(header.sequence);
}

void UartOTAReceiver::handleOTAData(const OTA::FrameHeader& header, const uint8_t* payload) {
    if (m_state != OTA::State::RECEIVING) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_INVALID_STATE);
        return;
    }
    
    // 检查序列号
    if (header.sequence != m_expectedSeq) {
        ESP_LOGW(TAG, "Sequence mismatch: expected %d, got %d", m_expectedSeq, header.sequence);
        sendNack(header.sequence, OTA::ErrorCode::ERR_SEQ);
        return;
    }
    
    // 检查偏移量
    if (header.offset != m_receivedBytes) {
        ESP_LOGW(TAG, "Offset mismatch: expected %lu, got %lu", m_receivedBytes, header.offset);
        sendNack(header.sequence, OTA::ErrorCode::ERR_OFFSET);
        return;
    }
    
    // 首包校验镜像头
    if (!m_imageHeaderChecked) {
        if (!checkImageHeader(payload, header.length)) {
            sendNack(header.sequence, OTA::ErrorCode::ERR_IMAGE_INVALID);
            abortOTA();
            return;
        }
        m_imageHeaderChecked = true;
    }
    
    // 写入OTA数据
    esp_err_t err = writeOTAData(payload, header.length);
    if (err != ESP_OK) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_FLASH_WRITE);
        abortOTA();
        return;
    }
    
    m_receivedBytes += header.length;
    m_expectedSeq++;
    
    // 计算进度
    uint8_t percent = (uint8_t)((m_receivedBytes * 100) / m_totalBytes);
    
    ESP_LOGD(TAG, "Received %lu/%lu bytes (%d%%)", m_receivedBytes, m_totalBytes, percent);
    
    // 回调进度
    if (m_progressCb) {
        m_progressCb(m_receivedBytes, m_totalBytes, percent);
    }
    
    // 每10%发送一次进度报告
    static uint8_t lastReportedPercent = 0;
    if (percent >= lastReportedPercent + 10 || percent == 100) {
        sendProgress(header.sequence);
        lastReportedPercent = percent;
    }
    
    sendAck(header.sequence);
}

void UartOTAReceiver::handleOTAEnd(const OTA::FrameHeader& header) {
    ESP_LOGI(TAG, "Received OTA_END command");
    
    if (m_state != OTA::State::RECEIVING) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_INVALID_STATE);
        return;
    }
    
    // 验证接收的数据量
    if (m_receivedBytes != m_totalBytes) {
        ESP_LOGE(TAG, "Size mismatch: received %lu, expected %lu", m_receivedBytes, m_totalBytes);
        sendNack(header.sequence, OTA::ErrorCode::ERR_OFFSET);
        abortOTA();
        return;
    }
    
    setState(OTA::State::VERIFYING);
    
    // 结束OTA
    esp_err_t err = endOTA();
    if (err != ESP_OK) {
        sendNack(header.sequence, OTA::ErrorCode::ERR_IMAGE_INVALID);
        return;
    }
    
    setState(OTA::State::COMPLETED);
    sendComplete(header.sequence);
    
    // 回调完成
    if (m_completeCb) {
        m_completeCb(true, OTA::ErrorCode::SUCCESS);
    }
    
    ESP_LOGI(TAG, "OTA completed successfully! Restarting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

void UartOTAReceiver::handleOTAAbort(const OTA::FrameHeader& header) {
    ESP_LOGW(TAG, "Received OTA_ABORT command");
    
    abortOTA();
    sendAck(header.sequence);
    
    if (m_completeCb) {
        m_completeCb(false, OTA::ErrorCode::ERR_UNKNOWN);
    }
}

void UartOTAReceiver::handleOTAQueryStatus(const OTA::FrameHeader& header) {
    sendStatus(header.sequence);
}

void UartOTAReceiver::handleOTARollback(const OTA::FrameHeader& header) {
    ESP_LOGW(TAG, "Received ROLLBACK request");
    
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* configured = esp_ota_get_boot_partition();
    
    if (running != configured) {
        ESP_LOGI(TAG, "Initiating rollback...");
        sendAck(header.sequence);
        vTaskDelay(pdMS_TO_TICKS(500));
        markAppInvalidAndRollback();
    } else {
        ESP_LOGW(TAG, "No previous partition to rollback to");
        sendNack(header.sequence, OTA::ErrorCode::ERR_ROLLBACK_FAILED);
    }
}

// ============== 响应发送 ==============

void UartOTAReceiver::sendFrame(OTA::Command cmd, uint16_t seq, const uint8_t* payload, uint16_t payloadLen) {
    uint8_t frameBuffer[OTA::MAX_FRAME_SIZE];
    size_t pos = 0;
    
    // 构建帧头
    OTA::FrameHeader header;
    OTA::buildFrameHeader(header, cmd, seq, 0, payloadLen);
    memcpy(frameBuffer, &header, sizeof(header));
    pos += sizeof(header);
    
    // 复制Payload
    if (payload && payloadLen > 0) {
        memcpy(frameBuffer + pos, payload, payloadLen);
        pos += payloadLen;
    }
    
    // 构建帧尾
    OTA::FrameFooter footer;
    OTA::buildFrameFooter(footer, frameBuffer, pos);
    memcpy(frameBuffer + pos, &footer, sizeof(footer));
    pos += sizeof(footer);
    
    // 发送
    uart_write_bytes(m_uartNum, frameBuffer, pos);
}

void UartOTAReceiver::sendAck(uint16_t seq, OTA::ErrorCode error) {
    OTA::AckPayload ack = {
        .errorCode = static_cast<uint8_t>(error),
        .expectedSeq = static_cast<uint16_t>(m_expectedSeq),
        .receivedBytes = m_receivedBytes
    };
    sendFrame(OTA::Command::OTA_ACK, seq, reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
}

void UartOTAReceiver::sendNack(uint16_t seq, OTA::ErrorCode error) {
    setError(error);
    OTA::AckPayload nack = {
        .errorCode = static_cast<uint8_t>(error),
        .expectedSeq = m_expectedSeq,
        .receivedBytes = m_receivedBytes
    };
    sendFrame(OTA::Command::OTA_NACK, seq, reinterpret_cast<uint8_t*>(&nack), sizeof(nack));
}

void UartOTAReceiver::sendReady(uint16_t seq) {
    sendFrame(OTA::Command::OTA_READY, seq, nullptr, 0);
}

void UartOTAReceiver::sendProgress(uint16_t seq) {
    OTA::ProgressPayload progress = {
        .receivedBytes = m_receivedBytes,
        .totalBytes = m_totalBytes,
        .percentage = static_cast<uint8_t>((m_receivedBytes * 100) / m_totalBytes)
    };
    sendFrame(OTA::Command::OTA_PROGRESS, seq, reinterpret_cast<uint8_t*>(&progress), sizeof(progress));
}

void UartOTAReceiver::sendComplete(uint16_t seq) {
    sendFrame(OTA::Command::OTA_COMPLETE, seq, nullptr, 0);
}

void UartOTAReceiver::sendStatus(uint16_t seq) {
    OTA::StatusPayload status;
    status.state = static_cast<uint8_t>(m_state);
    status.errorCode = static_cast<uint8_t>(m_lastError);
    status.receivedBytes = m_receivedBytes;
    status.totalBytes = m_totalBytes;
    
    // 获取当前运行版本
    const esp_app_desc_t* appDesc = esp_app_get_description();
    if (appDesc) {
        strncpy(reinterpret_cast<char*>(status.currentVersion), appDesc->version, sizeof(status.currentVersion) - 1);
    }
    
    sendFrame(OTA::Command::OTA_STATUS_RESP, seq, reinterpret_cast<uint8_t*>(&status), sizeof(status));
}

// ============== OTA操作 ==============

esp_err_t UartOTAReceiver::beginOTA() {
    ESP_LOGI(TAG, "Beginning OTA update...");
    
    // 获取当前运行分区信息
    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s (offset 0x%lX)", running->label, running->address);
    
    // 获取下一个OTA分区
    m_updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (m_updatePartition == nullptr) {
        ESP_LOGE(TAG, "No OTA partition available");
        setError(OTA::ErrorCode::ERR_PARTITION);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Update partition: %s (offset 0x%lX, size %lu)", 
             m_updatePartition->label, m_updatePartition->address, m_updatePartition->size);
    
    // 检查固件大小
    if (m_totalBytes > m_updatePartition->size) {
        ESP_LOGE(TAG, "Firmware too large: %lu > %lu", m_totalBytes, m_updatePartition->size);
        setError(OTA::ErrorCode::ERR_NO_MEMORY);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 开始OTA
    esp_err_t err = esp_ota_begin(m_updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &m_otaHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        setError(OTA::ErrorCode::ERR_FLASH_ERASE);
        return err;
    }
    
    ESP_LOGI(TAG, "OTA begin successful");
    return ESP_OK;
}

esp_err_t UartOTAReceiver::writeOTAData(const uint8_t* data, size_t length) {
    esp_err_t err = esp_ota_write(m_otaHandle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        setError(OTA::ErrorCode::ERR_FLASH_WRITE);
    }
    return err;
}

esp_err_t UartOTAReceiver::endOTA() {
    ESP_LOGI(TAG, "Ending OTA update...");
    
    esp_err_t err = esp_ota_end(m_otaHandle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed");
            setError(OTA::ErrorCode::ERR_IMAGE_INVALID);
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            setError(OTA::ErrorCode::ERR_UNKNOWN);
        }
        return err;
    }
    
    // 设置启动分区
    err = esp_ota_set_boot_partition(m_updatePartition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        setError(OTA::ErrorCode::ERR_PARTITION);
        return err;
    }
    
    ESP_LOGI(TAG, "OTA end successful, boot partition set to: %s", m_updatePartition->label);
    return ESP_OK;
}

void UartOTAReceiver::abortOTA() {
    if (m_otaHandle) {
        esp_ota_abort(m_otaHandle);
        m_otaHandle = 0;
    }
    
    m_receivedBytes = 0;
    m_totalBytes = 0;
    m_expectedSeq = 0;
    m_imageHeaderChecked = false;
    
    setState(OTA::State::IDLE);
    ESP_LOGW(TAG, "OTA aborted");
}

// ============== 镜像校验 ==============

bool UartOTAReceiver::checkImageHeader(const uint8_t* data, size_t length) {
    if (length < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGW(TAG, "First block too small to contain image header");
        return false;
    }
    
    // 解析新固件信息
    const esp_app_desc_t* newAppInfo = reinterpret_cast<const esp_app_desc_t*>(
        data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)
    );
    
    ESP_LOGI(TAG, "New firmware version: %s", newAppInfo->version);
    ESP_LOGI(TAG, "New firmware project: %s", newAppInfo->project_name);
    ESP_LOGI(TAG, "New firmware date: %s %s", newAppInfo->date, newAppInfo->time);
    
    // 获取当前运行固件信息
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t runningAppInfo;
    if (esp_ota_get_partition_description(running, &runningAppInfo) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", runningAppInfo.version);
        
        // 可选：版本检查
        // if (memcmp(newAppInfo->version, runningAppInfo.version, sizeof(newAppInfo->version)) == 0) {
        //     ESP_LOGW(TAG, "New version same as running version");
        //     return false;
        // }
    }
    
    // 检查上次失败的分区
    const esp_partition_t* lastInvalid = esp_ota_get_last_invalid_partition();
    if (lastInvalid != nullptr) {
        esp_app_desc_t invalidAppInfo;
        if (esp_ota_get_partition_description(lastInvalid, &invalidAppInfo) == ESP_OK) {
            if (memcmp(invalidAppInfo.version, newAppInfo->version, sizeof(newAppInfo->version)) == 0) {
                ESP_LOGW(TAG, "New version same as last invalid version: %s", invalidAppInfo.version);
                ESP_LOGW(TAG, "This firmware previously failed, refusing to install");
                return false;
            }
        }
    }
    
    return true;
}
