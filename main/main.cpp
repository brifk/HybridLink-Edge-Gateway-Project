#include "esp_log.h"
#include <memory>
#include <vector>

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

void print_system_task_stats()
{
    // 1. 分配一个足够大的 Buffer 来存储文本信息
    // 每个任务大概占用 40~50 字节，假设你有 20 个任务，1KB 足够了
    char *task_list_buffer = (char *)malloc(1024);

    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "无法分配内存给 Task List");
        return;
    }

    // 2. 获取任务列表信息
    vTaskList(task_list_buffer);

    // 3. 打印表头
    printf("\n=======================================================\n");
    printf("%-20s %-7s %-7s %-10s %-5s\n", "Task Name", "State", "Prio", "StackLeft", "Num");
    printf("-------------------------------------------------------\n");

    // 4. 打印内容
    // 这里的 StackLeft 是指"历史最小剩余栈空间" (High Water Mark)
    // 如果这个数值接近 0，说明该任务堆栈马上要溢出了，非常危险！
    printf("%s", task_list_buffer);
    printf("=======================================================\n");

    // 5. 释放内存
    free(task_list_buffer);
}

extern "C" void app_main()
{
    //  创建bno055对象以及相关任务
    auto bno055 = std::make_shared<Bno055Driver>();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(bno055);
    auto bno055_read_liner_acc_z_task = std::make_unique<Bno055ReadLinerAccZTask>(bno055);
    // 创建两个led对象，以及相关任务
    std::vector<std::shared_ptr<LED>> led_list;
    auto red_led = std::make_shared<LED>(LED_RED);
    auto green_led = std::make_shared<LED>(LED_GREEN);
    led_list.push_back(std::move(red_led));
    led_list.push_back(std::move(green_led));
    auto led_task = std::make_unique<LEDTask>(std::move(led_list));
    // 创建DSP引擎对象以及相关任务
    auto dsp_engine = std::make_shared<DSPEngine>(bno055);
    // 创建MQTT对象和相关任务
    auto mqtt_client = std::make_shared<MQTTClient>();
    auto mqtt_task = std::make_shared<MQTTTask>(mqtt_client, bno055);
    auto mqtt_notify_start_task = std::make_shared<MQTTNotifyStartTask>(mqtt_client);
    auto mqtt_notify_stop_task = std::make_shared<MQTTNotifyStopTask>(mqtt_client);
    // 创建Wifi对象以及相关任务
    auto wifi_station = std::make_unique<WifiStation>(mqtt_task, mqtt_notify_start_task, mqtt_notify_stop_task);
    auto wifi_task = std::make_unique<WifiTask>(std::move(wifi_station));

    // 任务启动
    bno055_read_euler_task->start();
    bno055_read_liner_acc_z_task->start();
    
    led_task->start();

    wifi_task->start();

    mqtt_task->start();
    mqtt_notify_start_task->start();
    mqtt_notify_stop_task->start();

    dsp_engine->start();
    
    while (1) {
        // print_system_task_stats();
        // ESP_LOGI("DEBUG", "Free Heap: %d", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

