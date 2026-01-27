#include "DSPEngine.hpp"
#include "MQTTClient.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
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
        bno055_euler_double_t batch_buffer[BATCH_SIZE];
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
                        mqtt_client->publish("bno055/euler_batch", json_payload);
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
