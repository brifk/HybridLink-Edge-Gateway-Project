#include "MQTTClient.hpp"

// 在文件末尾添加静态成员变量的定义
MQTTClient::mqtt_status_t MQTTClient::status = MQTTClient::DISCONNECTED;

void MQTTClient::log_error_if_nonzero(const char* message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "%s: %d", message, error_code);
    }
}

void MQTTClient::init()
{
    esp_mqtt_client_config_t mqtt_cfg;
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URL;
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void MQTTClient::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        status = CONNECTED;
        break;
    case MQTT_EVENT_DISCONNECTED:
        status = DISCONNECTED;
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_SUBSCRIBED:
        // TODO: 处理订阅成功事件
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_ERROR:
        break;
    default:
        break;
    }
}

void MQTTClient::publish(const char* topic, const char* payload)
{
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
}

void MQTTClient::subscribe(const char* topic)
{
    esp_mqtt_client_subscribe(client, topic, 0);
}

void MQTTClient::unsubscribe(const char* topic)
{
    esp_mqtt_client_unsubscribe(client, topic);
}

void MQTTClient::connect()
{
    esp_mqtt_client_reconnect(client);
}

void MQTTClient::disconnect()
{
    esp_mqtt_client_disconnect(client);
}
