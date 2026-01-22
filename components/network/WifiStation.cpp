#include "WifiStation.hpp"

uint8_t WifiStation::wifi_sta_status = WIFI_DISCONNECTED;

void WifiStation::init()
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init()); // 初始化TCP/IP栈
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建默认事件循环
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta(); // 创建默认的WiFi STA网络接口
        assert(sta_netif);
    }
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // 注册事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, this, NULL));
    // 设置WiFi模式为station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi已初始化");
}

void WifiStation::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiStation* wifi_station = (WifiStation*)arg;
    wifi_station->handle_event(event_base, event_id, event_data);
}

void WifiStation::handle_event(esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_sta_status = WIFI_DISCONNECTED;
        // TODO: 断开连接后通知MQTTClient任务
        mqtt_notify_stop_task->notify_stop();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "WiFi已连接，获取到IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_sta_status = WIFI_CONNECTED;
        // TODO: 连接成功后通知MQTTClient任务
        mqtt_notify_start_task->notify_start(); // 不是首次连接调用reconnect()
        mqtt_task->mqtt_start();    // 是首次连接调用mqtt_start()
    } else {
        ESP_LOGI(TAG, "其他WiFi事件: %d", event_id);
    }
}

uint8_t WifiStation::get_wifi_status() {
    return wifi_sta_status;
}