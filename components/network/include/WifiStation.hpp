#pragma once

#include "APPConfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "MQTTClient.hpp"
#include "MQTTTask.hpp"
#include <memory>

class WifiStation {
public:
    WifiStation(std::shared_ptr<MQTTTask> mqtt_task,
                std::shared_ptr<MQTTNotifyStartTask> mqtt_notify_start_task,
                std::shared_ptr<MQTTNotifyStopTask> mqtt_notify_stop_task) :
                mqtt_task(std::move(mqtt_task)),
                mqtt_notify_start_task(std::move(mqtt_notify_start_task)),
                mqtt_notify_stop_task(std::move(mqtt_notify_stop_task)) {
        sta_netif = nullptr;
    };
    ~WifiStation() {};
    enum WIFISTATUS {
        WIFI_DISCONNECTED,
        WIFI_CONNECTING,
        WIFI_CONNECTED,
        WIFI_FAILED,
        WIFI_SCANNING,
    };  
    void init();
    uint8_t get_wifi_status();

private:
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data);
    void handle_event(esp_event_base_t event_base, int32_t event_id, void* event_data);
    std::shared_ptr<MQTTTask> mqtt_task;
    std::shared_ptr<MQTTNotifyStartTask> mqtt_notify_start_task;
    std::shared_ptr<MQTTNotifyStopTask> mqtt_notify_stop_task;
    esp_netif_t* sta_netif;
    static uint8_t wifi_sta_status;
    static constexpr auto TAG = "WifiStation";
    typedef struct
    {
        uint8_t ssid[32];
        uint8_t password[64];
    } user_wifi_cfg;

    typedef struct
    {
        char ip[16];
        char netmask[16];
        char gw[16];
    } wifi_ip_info_t;
};