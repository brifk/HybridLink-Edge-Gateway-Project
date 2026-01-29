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
    enum class STATUS {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED,
        SCANNING,
    };  
    void init();
    STATUS get_wifi_status();

private:
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data);
    void handle_event(esp_event_base_t event_base, int32_t event_id, void* event_data);
    std::shared_ptr<MQTTTask> mqtt_task;
    std::shared_ptr<MQTTNotifyStartTask> mqtt_notify_start_task;
    std::shared_ptr<MQTTNotifyStopTask> mqtt_notify_stop_task;
    esp_netif_t* sta_netif;
    static STATUS wifi_sta_status;
    static constexpr auto TAG = "WifiStation";
};