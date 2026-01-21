#pragma once

#include "APPconfig.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "led.hpp"
#include "nvs_flash.h"
#include "mqtt_client.h"

class MQTTClient {
public:
    MQTTClient() { };
    ~MQTTClient() { };
    void init();
    void publish(const char* topic, const char* payload);
    void subscribe(const char* topic);
    void unsubscribe(const char* topic);
    void mqtt_start();
    void connect();
    void disconnect();
    enum mqtt_status_t {
        CONNECTED = 0,
        DISCONNECTED = 1,
    };
    mqtt_status_t get_status() { return status; };

private:
    static mqtt_status_t status;
    static constexpr auto TAG = "MQTTClient";
    esp_mqtt_client_handle_t client;
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    static void log_error_if_nonzero(const char* message, int error_code);
};
