#include "Thread.hpp"
#include "WifiStation.hpp"
#include <string.h>

class WifiTask : public Thread {
public:
    WifiTask(std::unique_ptr<WifiStation> wifi_station)
        : Thread("WifiTask", 1024 * 10, 5, 0)
        , wifi_station(std::move(wifi_station)) { };
    ~WifiTask() { };
    void run() override
    {
        wifi_station->init();
        wifi_config_t wifi_config;
        strcpy((char*)wifi_config.sta.ssid, SSID);
        strcpy((char*)wifi_config.sta.password, PASSWORD);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        static uint16_t retry_count = 0; // 重试次数
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        retry_interval = pdMS_TO_TICKS(10000);
        while (1) {
            if (wifi_station->get_wifi_status() == WifiStation::WIFI_CONNECTED) {
                // TODO: 连接成功后的处理
                retry_count = 0;
                vTaskDelay(retry_interval); // 10秒检查一次连接状态
            } else if (wifi_station->get_wifi_status() == WifiStation::WIFI_DISCONNECTED) {
                // TODO: 断开连接后的处理
                retry_count++;
                ESP_LOGI(TAG, "第%d次尝试连接WiFi...", retry_count);
                if (retry_count >= 12) {
                    ESP_LOGI(TAG, "两分钟未连接上WiFi，转为长时间重连模式");
                    retry_interval = pdMS_TO_TICKS(60000); // 60秒重试间隔
                }
                ESP_ERROR_CHECK(esp_wifi_connect());
                vTaskDelay(retry_interval);
            }
            ESP_LOGI(TAG, "WifiTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    };

private:
    std::unique_ptr<WifiStation> wifi_station;
    TickType_t retry_interval; // 10秒重试间隔
    static constexpr auto TAG = "WifiTask";
};
