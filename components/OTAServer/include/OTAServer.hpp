#pragma once

#include "Thread.hpp"
#include "OTAProtocol.hpp"
#include "UartOTAReceiver.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include <string>

/**
 * @brief HTTP OTA服务器类 (保留原有功能)
 */
class OTAServer : public Thread {
public:
    OTAServer()
        : Thread("OTAServer", 1024 * 10, tskIDLE_PRIORITY + 5, 0) { };
    ~OTAServer() { };
    void run() override
    {
        ESP_LOGI(TAG, "Downloading firmware from: %s", this->url.c_str());

        esp_http_client_config_t http_config = {};
        // 1. 设置 URL (例如 http://192.168.x.x:8000/firmware.bin)
        http_config.url = this->url.c_str();

        // 3. 【必须】跳过 Common Name 检查 (虽然 HTTP 不查证书，但加上防止底层库报错)
        // 某些版本的 IDF 在处理 HTTP OTA 时可能仍会检查这个标志位
        // 即使是 HTTP，加上这行也是安全的，能避免 "CN mismatch" 错误
        http_config.skip_cert_common_name_check = true;

        // 4. 将配置传入 OTA 结构体
        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &http_config;

        // 开始 OTA
        esp_err_t ret = esp_https_ota(&ota_config);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA Update Successful! Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000)); // 等待日志打印完
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA Update Failed. Error: %s", esp_err_to_name(ret));
            // 这里可以添加逻辑：比如发送 MQTT 消息通知服务器“升级失败”
        }
    };
    void setURL(const std::string& url) { this->url = url; }

private:
    static constexpr auto TAG = "OTAServer";
    std::string url;
};