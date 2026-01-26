#include "DSPEngine.hpp"
#include "MQTTClient.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include <memory>

class MQTTTask : public Thread {
public:
    MQTTTask(std::shared_ptr<MQTTClient> mqtt_client, std::shared_ptr<Bno055Driver> bno055, std::shared_ptr<DSPEngine> dsp_engine)
        : Thread("MQTTTask", 1024 * 5, PRIO_MQTT, 0)
        , mqtt_client(std::move(mqtt_client))
        , bno055(std::move(bno055))
        , dsp_engine(std::move(dsp_engine)) { };
    ~MQTTTask() { };
    void run() override
    {
        mqtt_client->init();
        while (1) {
            if (mqtt_client->get_status() == MQTTClient::CONNECTED) {
                float dsp_data[128];
                if (xQueueReceive(dsp_engine->get_dsp_queue_handle(), &dsp_data, portMAX_DELAY)) {
                    std::string dsp_str = "";
                    for (int i = 0; i < 128; i++) {
                        if (i > 0) {
                            dsp_str += ",";
                        }
                        char temp_buffer[20]; 
                        snprintf(temp_buffer, sizeof(temp_buffer), "%.2f", dsp_data[i]);
                        dsp_str += temp_buffer;

                        // 防止字符串过长
                        if (dsp_str.length() >= 2000) {
                            break;
                        }
                    }
                    mqtt_client->publish("bno055/dsp", dsp_str.c_str());
                }
                ESP_LOGI(TAG, "MQTTTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10)); // 未连接的时候不能一直占着cpu
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
