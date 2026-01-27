#pragma once

#include "Thread.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include <string>
#include <functional>

// OTA 状态枚举
enum class OTAStatus {
    IDLE,           // 空闲
    DOWNLOADING,    // 下载中
    VERIFYING,      // 验证中
    SUCCESS,        // 成功，准备重启
    FAILED          // 失败
};

// OTA 状态回调类型
using OTAStatusCallback = std::function<void(OTAStatus status, int progress, const char* message)>;

/**
 * @brief HTTP OTA 服务器类
 * 
 * 从 OrangePi HTTP 服务器下载固件并执行 OTA 升级
 * OTA 链路: 云端 -> OrangePi(4G下载) -> HTTP服务器 -> ESP32(WiFi HTTP下载)
 */
class OTAServer : public Thread {
public:
    OTAServer()
        : Thread("OTAServer", 1024 * 10, tskIDLE_PRIORITY + 5, 0)
        , statusCallback(nullptr)
        , autoReboot(true) { }
    
    ~OTAServer() { }
    
    /**
     * @brief 设置固件下载 URL
     * @param url HTTP URL (例如 http://192.168.4.1:8000/firmware.bin)
     */
    void setURL(const std::string& url) { this->url = url; }
    
    /**
     * @brief 设置状态回调函数
     * @param callback 状态变更时调用的回调
     */
    void setStatusCallback(OTAStatusCallback callback) { this->statusCallback = callback; }
    
    /**
     * @brief 设置是否自动重启
     * @param autoReboot true = OTA成功后自动重启
     */
    void setAutoReboot(bool autoReboot) { this->autoReboot = autoReboot; }
    
    /**
     * @brief 获取当前固件版本
     * @return 版本字符串
     */
    static const char* getCurrentVersion() {
        const esp_app_desc_t* app_desc = esp_app_get_description();
        return app_desc->version;
    }
    
    /**
     * @brief 标记当前应用程序有效（OTA 成功后调用）
     * @return true 成功
     */
    static bool markAppValid() {
        esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
        if (ret == ESP_OK) {
            ESP_LOGI("OTAServer", "Application marked as valid");
            return true;
        } else {
            ESP_LOGE("OTAServer", "Failed to mark app valid: %s", esp_err_to_name(ret));
            return false;
        }
    }
    
    /**
     * @brief 标记当前应用程序无效并回滚
     */
    static void markAppInvalidAndRollback() {
        ESP_LOGW("OTAServer", "Marking application as invalid and rolling back...");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    
    /**
     * @brief 执行 OTA 诊断检查
     * @return true 应用程序正常
     */
    static bool performDiagnostic() {
        // 可以添加自定义诊断逻辑
        // 例如：检查关键外设、网络连接等
        ESP_LOGI("OTAServer", "Performing OTA diagnostic...");
        return true;  // 默认通过
    }
    
    /**
     * @brief 获取当前运行分区信息
     */
    static void printPartitionInfo() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        ESP_LOGI("OTAServer", "Running partition: %s, addr: 0x%lx, size: 0x%lx",
                 running->label, running->address, running->size);
        
        const esp_app_desc_t* app_desc = esp_app_get_description();
        ESP_LOGI("OTAServer", "Firmware version: %s", app_desc->version);
        ESP_LOGI("OTAServer", "Project name: %s", app_desc->project_name);
        ESP_LOGI("OTAServer", "Compile time: %s %s", app_desc->date, app_desc->time);
    }
    
    /**
     * @brief 主运行函数 - 执行 OTA 升级
     */
    void run() override {
        ESP_LOGI(TAG, "========== Starting HTTP OTA ==========");
        ESP_LOGI(TAG, "Current version: %s", getCurrentVersion());
        ESP_LOGI(TAG, "Firmware URL: %s", this->url.c_str());
        printPartitionInfo();
        
        notifyStatus(OTAStatus::DOWNLOADING, 0, "Starting download");
        
        // HTTP 客户端配置
        esp_http_client_config_t http_config = {};
        http_config.url = this->url.c_str();
        http_config.skip_cert_common_name_check = true;
        http_config.timeout_ms = 30000;  // 30秒超时
        http_config.buffer_size = 4096;  // 增大缓冲区
        
        // OTA 配置
        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &http_config;
        
        // 使用高级 API 以获取进度
        esp_https_ota_handle_t ota_handle = nullptr;
        esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
            notifyStatus(OTAStatus::FAILED, 0, esp_err_to_name(ret));
            return;
        }
        
        // 获取固件大小
        int image_size = esp_https_ota_get_image_size(ota_handle);
        ESP_LOGI(TAG, "Firmware size: %d bytes", image_size);
        
        // 下载并写入
        int bytes_written = 0;
        int last_progress = 0;
        
        while (true) {
            ret = esp_https_ota_perform(ota_handle);
            
            if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                bytes_written = esp_https_ota_get_image_len_read(ota_handle);
                int progress = (image_size > 0) ? (bytes_written * 100 / image_size) : 0;
                
                // 每 10% 报告一次进度
                if (progress >= last_progress + 10) {
                    last_progress = (progress / 10) * 10;
                    ESP_LOGI(TAG, "Download progress: %d%% (%d/%d bytes)", 
                             progress, bytes_written, image_size);
                    notifyStatus(OTAStatus::DOWNLOADING, progress, "Downloading");
                }
                continue;
            }
            break;
        }
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(ret));
            esp_https_ota_abort(ota_handle);
            notifyStatus(OTAStatus::FAILED, 0, esp_err_to_name(ret));
            return;
        }
        
        notifyStatus(OTAStatus::VERIFYING, 100, "Verifying");
        
        // 验证并完成
        if (!esp_https_ota_is_complete_data_received(ota_handle)) {
            ESP_LOGE(TAG, "Complete data was not received");
            esp_https_ota_abort(ota_handle);
            notifyStatus(OTAStatus::FAILED, 0, "Incomplete data");
            return;
        }
        
        ret = esp_https_ota_finish(ota_handle);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "========== OTA Update Successful! ==========");
            notifyStatus(OTAStatus::SUCCESS, 100, "Update successful");
            
            if (autoReboot) {
                ESP_LOGI(TAG, "Rebooting in 3 seconds...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
            }
        } else if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed");
            notifyStatus(OTAStatus::FAILED, 0, "Validation failed");
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(ret));
            notifyStatus(OTAStatus::FAILED, 0, esp_err_to_name(ret));
        }
    }

private:
    static constexpr auto TAG = "OTAServer";
    std::string url;
    OTAStatusCallback statusCallback;
    bool autoReboot;
    
    void notifyStatus(OTAStatus status, int progress, const char* message) {
        if (statusCallback) {
            statusCallback(status, progress, message);
        }
    }
};
