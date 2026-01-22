#include "MQTTClient.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include <memory>

class MQTTTask : public Thread {
public:
    MQTTTask(std::shared_ptr<MQTTClient> mqtt_client, std::shared_ptr<Bno055Driver> bno055)
        : Thread("MQTTTask", 1024 * 5, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client))
        , bno055(std::move(bno055)) { };
    ~MQTTTask() { };
    void run() override
    {
        mqtt_client->init();
        while (1) {
            if (mqtt_client->get_status() == MQTTClient::CONNECTED) {
                bno055_euler_double_t euler;
                if(xQueueReceive(bno055->get_euler_queue_handle(), &euler, 0)) {    // 非阻塞式接收
                    // TODO: 后面改成json格式
                    char euler_str[128];
                    snprintf(euler_str, sizeof(euler_str), "{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f}", euler.r, euler.p, euler.h);
                    mqtt_client->publish("bno055/euler", euler_str);
                }
                ESP_LOGI(TAG, "MQTTTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
            }
        }
    };

    void mqtt_start()
    {
        if (!mqtt_client->get_connected()) { // 未连接时才启动
            mqtt_client->mqtt_start();
            mqtt_client->set_connected(true);
            ESP_LOGI(TAG, "MQTT客户端已启动");
        }
    };

private:
    static constexpr auto TAG = "MQTTTask";
    std::shared_ptr<MQTTClient> mqtt_client;
    std::shared_ptr<Bno055Driver> bno055;
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
