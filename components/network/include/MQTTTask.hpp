#include "Thread.hpp"
#include "MQTTClient.hpp"
#include <memory>

class MQTTTask : public Thread
{
public:
    MQTTTask(std::shared_ptr<MQTTClient> mqtt_client) :
        Thread("MQTTTask", 1024 * 4, tskIDLE_PRIORITY + 6, 0),
        mqtt_client(std::move(mqtt_client))
    {};
    ~MQTTTask() {};
    void run() override {
        mqtt_client->init();
        while(1) {
            if (mqtt_client->get_status() == MQTTClient::CONNECTED) {
                // TODO: 发布MQTT消息
                ESP_LOGI(TAG, "MQTTTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));  
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    };
    
    void mqtt_start() {
        mqtt_client->mqtt_start();
        ESP_LOGI(TAG, "MQTT客户端已启动");
    };
private:
    static constexpr auto TAG = "MQTTTask";
    std::shared_ptr<MQTTClient> mqtt_client;
};

// 由于Wifi的连接与断开是在中断中，所以需要使用任务通知来触发MQTT连接与断开
class MQTTNotifyStartTask : public Thread
{
public:
    MQTTNotifyStartTask(std::shared_ptr<MQTTClient> mqtt_client) :
        Thread("MQTTNotifyStartTask", 1024 * 3, tskIDLE_PRIORITY + 4, 0),
        mqtt_client(std::move(mqtt_client))
    {};
    ~MQTTNotifyStartTask() {};
    void run() override {
        while(1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            mqtt_client->connect();
        }
    };
private:
    std::shared_ptr<MQTTClient> mqtt_client;
};

class MQTTNotifyStopTask : public Thread
{
public:
    MQTTNotifyStopTask(std::shared_ptr<MQTTClient> mqtt_client) :
        Thread("MQTTNotifyStopTask", 1024 * 3, tskIDLE_PRIORITY + 4, 0),
        mqtt_client(std::move(mqtt_client))
    {};
    ~MQTTNotifyStopTask() {};
    void run() override {
        while(1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            mqtt_client->disconnect();
        }
    };
private:
    std::shared_ptr<MQTTClient> mqtt_client;
};
