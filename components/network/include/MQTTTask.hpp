#include "DSPEngine.hpp"
#include "MQTTClient.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include "OTAServer.hpp"
#include <cJSON.h>
#include <memory>
#include <unordered_map>

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
            vTaskDelay(pdMS_TO_TICKS(10));
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
//增加了普通任务中触发通知的函数
    void notify_start()
    {
        if (this->getHandle() != NULL) {
            xTaskNotifyGive(this->getHandle());
        }
    };
//中断中触发通知的函数
    void notify_start_FromISR()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (this->getHandle() != NULL) {
            vTaskNotifyGiveFromISR(this->getHandle(), &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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

    void run() override {
        std::string* rx_msg_ptr = nullptr;
        while (1) {
            if (xQueueReceive(mqtt_client->get_event_queue_handle(), &rx_msg_ptr, portMAX_DELAY)) {
                if (rx_msg_ptr == nullptr) continue;

                // --- 内存安全卫士 ---
                // 确保 rx_msg_ptr 在本次循环结束时自动 delete
                std::unique_ptr<std::string> msg_guard(rx_msg_ptr);

                // 解析 JSON
                cJSON* raw_root = cJSON_Parse(rx_msg_ptr->c_str());
                if (raw_root == nullptr) {
                    ESP_LOGE(TAG, "JSON Parse Error");
                    continue;
                }
                // 确保 root 及其子节点在本次循环结束时自动 cJSON_Delete
                std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(raw_root, &cJSON_Delete);

                // --- 业务处理 ---
                process_json_cmd(root.get());
            }
            ESP_LOGI(TAG, "Stack High Water Mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    }

private:
    static constexpr auto TAG = "MQTTSubscribeTask";
    std::shared_ptr<MQTTClient> mqtt_client;
    std::shared_ptr<Bno055Driver> bno055;
    std::vector<std::shared_ptr<LED>> led_list;

    // 内部解析逻辑抽离
    void process_json_cmd(cJSON* root) {
        cJSON* cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
        if (!cJSON_IsString(cmd_item) || (cmd_item->valuestring == nullptr)) return;

        const char* cmd = cmd_item->valuestring;

        if (strcmp(cmd, "ota") == 0) {
            ESP_LOGI(TAG, "OTA command received");
            handle_ota_command(root);
        } 
        else if (strcmp(cmd, "led") == 0) {
            handle_led_command(root);
        } 
        else if (strcmp(cmd, "bno055") == 0) {
            handle_bno055_command(root);
        } 
        else {
            ESP_LOGE(TAG, "Unknown command: %s", cmd);
        }
    }

    void handle_led_command(cJSON* root) {
        cJSON* color_item = cJSON_GetObjectItemCaseSensitive(root, "color");
        cJSON* state_item = cJSON_GetObjectItemCaseSensitive(root, "state");

        if (!cJSON_IsString(color_item) || !cJSON_IsString(state_item)) {
            ESP_LOGE(TAG, "Invalid LED params");
            return;
        }

        // 颜色解析
        led_color_t color;
        if (strcmp(color_item->valuestring, "RED") == 0) color = LED_RED;
        else if (strcmp(color_item->valuestring, "GREEN") == 0) color = LED_GREEN;
        else {
            ESP_LOGE(TAG, "Invalid color: %s", color_item->valuestring);
            return;
        }

        // 状态解析映射表（让代码更整洁）
        static const std::unordered_map<std::string, led_state_t> state_map = {
            {"ON", LED_STATE_ON}, {"OFF", LED_STATE_OFF},
            {"BLINK_SLOW", LED_STATE_BLINK_SLOW}, {"BLINK_FAST", LED_STATE_BLINK_FAST},
            {"BLINK_DOUBLE", LED_STATE_BLINK_DOUBLE}, {"BREATH", LED_STATE_BREATH}
        };

        auto it = state_map.find(state_item->valuestring);
        if (it != state_map.end()) {
            led_list[color]->set(it->second);
            ESP_LOGI(TAG, "LED %s set to %s", color_item->valuestring, state_item->valuestring);
        } else {
            ESP_LOGE(TAG, "Invalid state: %s", state_item->valuestring);
        }
    }

    void handle_bno055_command(cJSON* root) {
        cJSON* state_item = cJSON_GetObjectItemCaseSensitive(root, "bno055_state");
        if (!cJSON_IsString(state_item)) return;

        using bno_st = Bno055Driver::bno055_euler_state_t;
        using bno_linear_accel_z_st = Bno055Driver::bno055_linear_accel_z_state_t;

        if (strcmp(state_item->valuestring, "RUNNING_EULER") == 0) 
            bno055->bno055_euler_state = bno_st::RUNNING_EULER;
        else if (strcmp(state_item->valuestring, "STOPPED_EULER") == 0) 
            bno055->bno055_euler_state = bno_st::STOPPED_EULER;
        else if (strcmp(state_item->valuestring, "RUNNING_LINEAR_ACCEL_Z") == 0) 
            bno055->bno055_linear_accel_z_state = bno_linear_accel_z_st::RUNNING_LINEAR_ACCEL_Z;
        else if (strcmp(state_item->valuestring, "STOPPED_LINEAR_ACCEL_Z") == 0) 
            bno055->bno055_linear_accel_z_state = bno_linear_accel_z_st::STOPPED_LINEAR_ACCEL_Z;
        ESP_LOGI(TAG, "bno055 state set to %s", state_item->valuestring);
    }

    void handle_ota_command(cJSON* root) {
        cJSON* url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
        if (!cJSON_IsString(url_item) || (url_item->valuestring == nullptr)) return;

        const char* url = url_item->valuestring;
        ESP_LOGI(TAG, "OTA URL: %s", url);
    }
};