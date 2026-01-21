#pragma once
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include "udp_client.h"
#include "my_mqtt_client.h"
#include "config.h"
#include "LED.h"

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

enum WIFISTATUS {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    WIFI_SCANNING,
};

// 声明任务句柄
extern TaskHandle_t wifi_connect_task_handle;

extern wifi_ap_record_t ap_info[16];
extern volatile uint8_t wifi_sta_status;
extern volatile uint8_t wifi_pwr_status; // 0: wifi off, 1: wifi on

// WiFi functions
void wifi_get_ip_info_str(wifi_ip_info_t* wifi_ip_info);
void wifi_init();
void wifi_connect_task(void* args);
void wifi_event_init(void);
void wifi_event_callback(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data);