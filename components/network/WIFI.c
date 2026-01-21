#include "WIFI.h"

#define TAG "wifi"

esp_netif_t* sta_netif = NULL;

volatile uint8_t wifi_sta_status = WIFI_DISCONNECTED;
TaskHandle_t wifi_connect_task_handle = NULL;

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

/*
 * @brief WiFi事件处理回调函数
 * @param arg 事件参数
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_sta_status = WIFI_DISCONNECTED;
#if CONFIG_ENABLE_MQTT
        NotifyStopMqttClientTask(); // 断开时通知停止MQTT客户端任务
#elif CONFIG_ENABLE_UDP
        NotifyStopUdpClientTask(); // 断开时通知停止UDP客户端任务
#endif
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "WiFi已连接，获取到IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
#if CONFIG_ENABLE_MQTT
        NotifyStartMqttClientTask(); // 连接成功后通知MQTT客户端任务
#elif CONFIG_ENABLE_UDP
        NotifyUdpClientTask(); // 连接成功后通知UDP客户端任务
#endif
        wifi_sta_status = WIFI_CONNECTED;
    } else {
        ESP_LOGI(TAG, "其他WiFi事件: %d", event_id);
    }
}

/*
 * @brief wifi连接任务
 * @param args wifi配置参数
 */
void wifi_connect_task(void* args)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        }
    };
    static u16_t retry_count = 0; // 重试次数
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    static TickType_t retry_interval = pdMS_TO_TICKS(10000); // 10秒重试间隔
    while (1) {
        if(wifi_sta_status == WIFI_CONNECTED) {
            led_set_state(LED_RED, LED_STATE_BREATH);
            retry_count = 0;
            vTaskDelay(retry_interval); // 10秒检查一次连接状态
        }
        else if(wifi_sta_status == WIFI_DISCONNECTED) {
            led_set_state(LED_RED, LED_STATE_BLINK_FAST);
            retry_count++;
            ESP_LOGI(TAG, "第%d次尝试连接WiFi...", retry_count);
            if(retry_count >= 12) {
                ESP_LOGI(TAG, "两分钟未连接上WiFi，转为长时间重连模式");
                retry_interval = pdMS_TO_TICKS(60000); // 60秒重试间隔
            }
            ESP_ERROR_CHECK(esp_wifi_connect());
            vTaskDelay(retry_interval);
        }
        ESP_LOGI(TAG, "wifi_connect_task stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    }
}

/*
 * @brief 初始化wifi,并启动wifi连接任务
 */
void wifi_init()
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
    // 创建事件组
    wifi_event_group = xEventGroupCreate();
    // 注册事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    // 设置WiFi模式为station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    // 创建wifi连接任务
    BaseType_t xReturned = xTaskCreate(wifi_connect_task,
        "wifi_connect_task",
        3 * 1024,
        NULL,
        tskIDLE_PRIORITY + 7,
        &wifi_connect_task_handle);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi_connect_task");
    }
}

void wifi_get_ip_info_str(wifi_ip_info_t* wifi_ip_info)
{
    if (sta_netif != NULL && wifi_sta_status == WIFI_CONNECTED) {
        esp_netif_ip_info_t netif_ip_info;
        esp_netif_get_ip_info(sta_netif, &netif_ip_info);
        sprintf(wifi_ip_info->ip, IPSTR, IP2STR(&netif_ip_info.ip));
        sprintf(wifi_ip_info->netmask, IPSTR, IP2STR(&netif_ip_info.netmask));
        sprintf(wifi_ip_info->gw, IPSTR, IP2STR(&netif_ip_info.gw));
    }
}