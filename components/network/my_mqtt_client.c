#include "my_mqtt_client.h"
#include "mqtt_client.h"

static const char* TAG = "MQTT_client";
static uint32_t cnt = 0;
esp_mqtt_client_handle_t clientHandle = NULL;
TaskHandle_t StartMqttClientTaskHandle = NULL;
TaskHandle_t StopMqttClientTaskHandle = NULL;
TaskHandle_t MqttPubTaskHandle = NULL;

extern QueueHandle_t bno055tQueue;

static void log_error_if_nonzero(const char* message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
MQTT_EVENT_BEFORE_CONNECT：客户端已初始化并即将开始连接至服务器。
MQTT_EVENT_CONNECTED：客户端已成功连接至服务器。客户端已准备好收发数据。
MQTT_EVENT_DISCONNECTED：由于无法读取或写入数据，例如因为服务器无法使用，客户端已终止连接。
MQTT_EVENT_SUBSCRIBED：服务器已确认客户端的订阅请求。事件数据将包含订阅消息的消息 ID。
MQTT_EVENT_UNSUBSCRIBED：服务器已确认客户端的退订请求。事件数据将包含退订消息的消息 ID。
MQTT_EVENT_PUBLISHED：服务器已确认客户端的发布消息。消息将仅针对 QoS 级别 1 和 2 发布，因为级别 0 不会进行确认。事件数据将包含发布消息的消息 ID。
MQTT_EVENT_DATA：客户端已收到发布消息。事件数据包含：消息 ID、发布消息所属主题名称、收到的数据及其长度。对于超出内部缓冲区的数据，将发布多个 MQTT_EVENT_DATA，并更新事件数据的 current_data_offset 和 total_data_len 以跟踪碎片化消息。
MQTT_EVENT_ERROR：客户端遇到错误。使用事件数据 error_handle 字段中的 error_type，可以发现错误。错误类型决定 error_handle 结构体的哪些部分会被填充。
*/
// esp32只用作发布，不接收订阅的消息
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client;
    // int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// 启动mqtt客户端，注册事件处理函数，在连接上WiFi后自动连接mqtt服务器
static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL
    };
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

    clientHandle = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(clientHandle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(clientHandle);
}

#if MQTT_LOSS_TEST
void mqtt_pub_task(void* pvParameters)
{
    mqtt_start();
    static uint32_t loss_cnt = 0;
    while (1) {
        char data[50];
        snprintf(data, sizeof(data), "msg_%ld_", loss_cnt);
        esp_mqtt_client_publish(clientHandle, "test/esp32_loss", data, 0, 0, 0);
        ++loss_cnt;
        vTaskDelay(pdMS_TO_TICKS(10));
        // ESP_LOGI(TAG, "MqttClientTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    }
}
#else
void add_sensor_data(cJSON* root, const char* name, double x, double y, double z)
{
    cJSON* sensor_obj = cJSON_CreateObject();
    if (strcmp(name, "euler") == 0) {
        cJSON_AddNumberToObject(sensor_obj, "h", x);
        cJSON_AddNumberToObject(sensor_obj, "r", y);
        cJSON_AddNumberToObject(sensor_obj, "p", z);
    } else {
        cJSON_AddNumberToObject(sensor_obj, "x", x);
        cJSON_AddNumberToObject(sensor_obj, "y", y);
        cJSON_AddNumberToObject(sensor_obj, "z", z);
    }
    cJSON_AddItemToObject(root, name, sensor_obj);
}

char* convert_bno055_to_json(struct bno055_data_t* data)
{
    // 1. 创建根对象
    cJSON* root = cJSON_CreateObject();
    // 2. 利用辅助函数添加各个部分，代码非常整洁
    // 注意：这里假设你的各个子结构体内存布局一致，直接取值即可
    add_sensor_data(root, "accel", data->accel.x, data->accel.y, data->accel.z);
    add_sensor_data(root, "mag", data->mag.x, data->mag.y, data->mag.z);
    add_sensor_data(root, "gyro", data->gyro.x, data->gyro.y, data->gyro.z);
    add_sensor_data(root, "euler", data->euler.h, data->euler.r, data->euler.p);
    add_sensor_data(root, "lin_acc", data->linear_accel.x, data->linear_accel.y, data->linear_accel.z);
    add_sensor_data(root, "grav", data->gravity.x, data->gravity.y, data->gravity.z);
    cJSON_AddNumberToObject(root, "cnt", cnt);
    // 3. 打印成字符串 (Unformatted 比较省空间，Formatted 比较方便阅读)
    char* json_string = cJSON_PrintUnformatted(root);

    // 4. 清理 cJSON 对象占用的堆内存（非常重要！否则内存泄漏）
    cJSON_Delete(root);

    return json_string; // 注意：使用完这个字符串后，外部需要 free(json_string)
}

void mqtt_pub_task(void* pvParameters)
{
    mqtt_start();
    char* bno055_data_json = NULL;
    static struct bno055_data_t bno055_data;
    while (1) {
        if (xQueueReceive(bno055tQueue, &bno055_data, portMAX_DELAY) == pdPASS) {
            bno055_data_json = convert_bno055_to_json(&bno055_data);
            esp_mqtt_client_publish(clientHandle, "sensor/bno055_left", bno055_data_json, 0, 0, 0);
            // esp_mqtt_client_publish(clientHandle, "sensor/bno055_right", bno055_data_json, 0, 0, 0);
            // ESP_LOGI(TAG, "publish bno055 data: %s", bno055_data_json);
            free(bno055_data_json);
            ++cnt;
        }
        // ESP_LOGI(TAG, "MqttClientTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    }
}
#endif

static void mqtt_stop(void)
{
    ESP_ERROR_CHECK(esp_mqtt_client_stop(clientHandle));
    ESP_ERROR_CHECK(esp_mqtt_client_destroy(clientHandle));
    clientHandle = NULL;
}

void StopMqttClientTask(void* pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "stop mqtt client task");
        vTaskDelete(MqttPubTaskHandle);
        MqttPubTaskHandle = NULL;
        mqtt_stop();
        ESP_LOGI(TAG, "StopMqttClientTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    }
}

void StartMqttClientTask(void* pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "start mqtt client task");
        xTaskCreatePinnedToCore(mqtt_pub_task,
            "mqtt_pub_task",
            4 * 1024,
            NULL, tskIDLE_PRIORITY + 6,
            &MqttPubTaskHandle,
            0); // MQTT发布任务
        ESP_LOGI(TAG, "StartMqttClientTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
    }
}

void NotifyStartMqttClientTask(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (StartMqttClientTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(StartMqttClientTaskHandle, &xHigherPriorityTaskWoken);
    }
}

void NotifyStopMqttClientTask(void)
{
    cnt = 0; // 断开连接需要重置计数器
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (StopMqttClientTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(StopMqttClientTaskHandle, &xHigherPriorityTaskWoken);
    }
}