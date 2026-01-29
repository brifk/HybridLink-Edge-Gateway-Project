#include "DSPEngine.hpp"
#include "MQTTClient.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include <cJSON.h>
#include <memory>

class MQTTTask : public Thread {
public:
    MQTTTask(std::shared_ptr<MQTTClient> mqtt_client, std::shared_ptr<Bno055Driver> bno055)
        : Thread("MQTTTask", 1024 * 5, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client))
        , bno055(std::move(bno055))
        , dsp_engine(std::move(dsp_engine)) { };
    ~MQTTTask() { };
    void run() override
    {
        mqtt_client->init();
        const int BATCH_SIZE = 10;
        // bno055_euler_double_t batch_buffer[BATCH_SIZE];
        std::vector<bno055_euler_double_t> batch_buffer(BATCH_SIZE);
        int current_count = 0;
        char json_payload[1024];

        while (1) {
            if (mqtt_client->get_status() == MQTTClient::CONNECTED) {
                bno055_euler_double_t euler;

                // 阻塞读取队列
                if (xQueueReceive(bno055->get_euler_queue_handle(), &euler, portMAX_DELAY)) {

                    // 1. 先存入缓存数组
                    batch_buffer[current_count] = euler;
                    current_count++;

                    // 2. 如果存满了 BATCH_SIZE (10条)，就开始打包发送
                    if (current_count >= BATCH_SIZE) {
                        int offset = 0; // 记录字符串当前写到哪了
                        offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "[");

                        for (int i = 0; i < BATCH_SIZE; i++) {
                            offset += snprintf(json_payload + offset, sizeof(json_payload) - offset,
                                "{\"r\":%.2f,\"p\":%.2f,\"h\":%.2f}",
                                batch_buffer[i].r,
                                batch_buffer[i].p,
                                batch_buffer[i].h);

                            // 如果不是最后一个元素，加逗号
                            if (i < BATCH_SIZE - 1) {
                                offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, ",");
                            }
                        }
                        offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "]");
                        mqtt_client->publish("bno055/euler", json_payload);
                        // --- 发送后处理 ---
                        current_count = 0; // 清零计数器
                    }
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    };

    void mqtt_start()
    {
        if (!mqtt_client->get_connected()) { // 未连接时才启动
            mqtt_client->mqtt_start();
            mqtt_client->set_connected(true);
        }
    };

private:
    static constexpr auto TAG = "MQTTTask";
    std::shared_ptr<MQTTClient> mqtt_client;
    std::shared_ptr<Bno055Driver> bno055;
    std::shared_ptr<DSPEngine> dsp_engine;
};

// 由于Wifi的连接与断开是在中断中，所以需要使用任务通知来触发MQTT连接与断开
class MQTTNotifyStartTask : public Thread {
public:
    MQTTNotifyStartTask(std::shared_ptr<MQTTClient> mqtt_client)
        : Thread("MQTTNotifyStartTask", 1024 * 3, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client)) { };
    ~MQTTNotifyStartTask() { };
    void run() override
    {
        while (1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            mqtt_client->connect();
        }
    };
    void notify_start()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (this->getHandle() != NULL) {
            vTaskNotifyGiveFromISR(this->getHandle(), &xHigherPriorityTaskWoken);
        }
    };

private:
    std::shared_ptr<MQTTClient> mqtt_client;
};

class MQTTNotifyStopTask : public Thread {
public:
    MQTTNotifyStopTask(std::shared_ptr<MQTTClient> mqtt_client)
        : Thread("MQTTNotifyStopTask", 1024 * 3, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client)) { };
    ~MQTTNotifyStopTask() { };
    void run() override
    {
        while (1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            mqtt_client->disconnect();
        }
    };

    void notify_stop()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (this->getHandle() != NULL) {
            vTaskNotifyGiveFromISR(this->getHandle(), &xHigherPriorityTaskWoken);
        }
    }

private:
    std::shared_ptr<MQTTClient> mqtt_client;
};

class MQTTSubscribeTask : public Thread {
public:
    MQTTSubscribeTask(std::shared_ptr<MQTTClient> mqtt_client, std::shared_ptr<Bno055Driver> bno055, std::vector<std::shared_ptr<LED>> led_list)
        : Thread("MQTTSubscribeTask", 1024 * 5, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client))
        , bno055(std::move(bno055))
        , led_list(std::move(led_list)) { };
    ~MQTTSubscribeTask() { };
    void run() override
    {
        std::string* rx_msg_ptr = nullptr;
        while (1) {
            if (xQueueReceive(mqtt_client->get_event_queue_handle(), &rx_msg_ptr, portMAX_DELAY)) {
                if (rx_msg_ptr != nullptr) {
                    cJSON* root = cJSON_Parse(rx_msg_ptr->c_str());

                    cJSON* cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
                    if (cJSON_IsString(cmd_item) && (cmd_item->valuestring != NULL)) {
                        // TODO: 解决内存泄露问题
                        if (strcmp(cmd_item->valuestring, "ota") == 0) {
                            ESP_LOGI(TAG, "OTA command received");
                        } else if (strcmp(cmd_item->valuestring, "led") == 0) {
                            ESP_LOGI(TAG, "LED command received");
                            cJSON* color_item = cJSON_GetObjectItemCaseSensitive(root, "color");
                            if (cJSON_IsString(color_item) && (color_item->valuestring != NULL)) {
                                led_color_t led_color;
                                if (strcmp(color_item->valuestring, "RED") == 0) {
                                    led_color = LED_RED;
                                } else if (strcmp(color_item->valuestring, "GREEN") == 0) {
                                    led_color = LED_GREEN;
                                } else {
                                    ESP_LOGE(TAG, "Invalid color: %s", color_item->valuestring);
                                    break;
                                }

                                // 解析状态参数
                                cJSON* state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
                                if (cJSON_IsString(state_item) && (state_item->valuestring != NULL)) {
                                    led_state_t led_state;
                                    if (strcmp(state_item->valuestring, "ON") == 0) {
                                        led_state = LED_STATE_ON;
                                    } else if (strcmp(state_item->valuestring, "OFF") == 0) {
                                        led_state = LED_STATE_OFF;
                                    } else if (strcmp(state_item->valuestring, "BLINK_SLOW") == 0) {
                                        led_state = LED_STATE_BLINK_SLOW;
                                    } else if (strcmp(state_item->valuestring, "BLINK_FAST") == 0) {
                                        led_state = LED_STATE_BLINK_FAST;
                                    } else if (strcmp(state_item->valuestring, "BLINK_DOUBLE") == 0) {
                                        led_state = LED_STATE_BLINK_DOUBLE;
                                    } else if (strcmp(state_item->valuestring, "BREATH") == 0) {
                                        led_state = LED_STATE_BREATH;
                                    } else {
                                        ESP_LOGE(TAG, "Invalid state: %s", state_item->valuestring);
                                        break;
                                    }

                                    // 设置LED状态
                                    led_list[led_color]->set(led_state);
                                    ESP_LOGI(TAG, "LED %s set to state %s", color_item->valuestring, state_item->valuestring);
                                } else {
                                    ESP_LOGE(TAG, "No valid state provided for LED command");
                                }
                            } else {
                                ESP_LOGE(TAG, "No valid color provided for LED command");
                            }
                        } else if (strcmp(cmd_item->valuestring, "bno055") == 0) {
                            cJSON* bno055_state_item = cJSON_GetObjectItemCaseSensitive(root, "bno055_state");
                            if (cJSON_IsString(bno055_state_item) && (bno055_state_item->valuestring != NULL)) {
                                Bno055Driver::bno055_state_t bno055_state;
                                if (strcmp(bno055_state_item->valuestring, "RUNNING_EULER") == 0) {
                                    bno055_state = Bno055Driver::bno055_state_t::RUNNING_EULER;
                                } else if (strcmp(bno055_state_item->valuestring, "RUNNING_LINEAR_ACCEL_Z") == 0) {
                                    bno055_state = Bno055Driver::bno055_state_t::RUNNING_LINEAR_ACCEL_Z;
                                } else if (strcmp(bno055_state_item->valuestring, "STOPPED_EULER") == 0) {
                                    bno055_state = Bno055Driver::bno055_state_t::STOPPED_EULER;
                                } else if (strcmp(bno055_state_item->valuestring, "STOPPED_LINEAR_ACCEL_Z") == 0) {
                                    bno055_state = Bno055Driver::bno055_state_t::STOPPED_LINEAR_ACCEL_Z;
                                } else {
                                    ESP_LOGE(TAG, "Invalid bno055_state: %s", bno055_state_item->valuestring);
                                    break;
                                }
                                bno055->bno055_state = bno055_state;
                                ESP_LOGI(TAG, "bno055 state set to %s", bno055_state_item->valuestring);
                            } else {
                                ESP_LOGE(TAG, "No valid bno055_state provided");
                            }
                        } else {
                            ESP_LOGE(TAG, "Unknown command: %s", cmd_item->valuestring);
                        }
                    }
                    delete rx_msg_ptr;
                    rx_msg_ptr = nullptr;
                } else {
                    ESP_LOGE(TAG, "Received NULL message");
                }
            }
            ESP_LOGI(TAG, "MQTTSubscribeTask Stack High Water Mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    };

private:
    static constexpr auto TAG = "MQTTSubscribeTask";
    std::shared_ptr<MQTTClient> mqtt_client;
    std::shared_ptr<Bno055Driver> bno055;
    std::vector<std::shared_ptr<LED>> led_list;
};