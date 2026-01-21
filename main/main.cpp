#include "esp_log.h"
#include <memory>
#include <stdio.h>

#include "APPConfig.h"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include "bno055task.hpp"
#include "led.hpp"
#include "ledtask.hpp"
#include "WifiTask.hpp"
#include "WifiStation.hpp"
#include "DSPEngine.hpp"

static constexpr auto TAG = "main";

extern "C" void app_main()
{
    //  创建bno055对象以及相关任务
    auto bno055 = std::make_shared<Bno055Driver>();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(bno055);
    auto bno055_read_liner_acc_z_task = std::make_unique<Bno055ReadLinerAccZTask>(bno055);
    // 创建两个led对象，以及相关任务
    auto red_led = std::make_unique<LED>(LED_RED);
    auto green_led = std::make_unique<LED>(LED_GREEN);
    auto red_led_task = std::make_unique<LEDTask>(std::move(red_led));
    auto green_led_task = std::make_unique<LEDTask>(std::move(green_led));
    // 创建MQTT对象和相关任务
    auto mqtt_client = std::make_shared<MQTTClient>();
    auto mqtt_task = std::make_unique<MQTTTask>(mqtt_client);
    auto mqtt_notify_start_task = std::make_shared<MQTTNotifyStartTask>(mqtt_client);
    auto mqtt_notify_stop_task = std::make_shared<MQTTNotifyStopTask>(mqtt_client);
    // 创建Wifi对象以及相关任务
    auto wifi_station = std::make_unique<WifiStation>(mqtt_notify_start_task, mqtt_notify_stop_task);
    auto wifi_task = std::make_unique<WifiTask>(std::move(wifi_station));
    // 任务启动
    bno055_read_euler_task->start();
    bno055_read_liner_acc_z_task->start();
    
    red_led_task->start();
    green_led_task->start();

    wifi_task->start();

    mqtt_task->start();
    mqtt_notify_start_task->start();
    mqtt_notify_stop_task->start();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}
