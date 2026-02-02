#include "esp_log.h"
#include <memory>
#include <vector>

#include "APPConfig.h"
#include "DSPEngine.hpp"
#include "Thread.hpp"
#include "WifiStation.hpp"
#include "WifiTask.hpp"
#include "bno055driver.hpp"
#include "bno055task.hpp"
#include "led.hpp"
#include "ledtask.hpp"

static constexpr auto TAG = "main";

void dump_system_status() {
    static const char *SYS_TAG = "SYS_CHECK";

    // --- 1. 内存余量监控 ---
    // 内部 RAM (最宝贵的资源)
    uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t min_internal  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    
    // 外部 PSRAM (如果有)
    uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(SYS_TAG, "---------------- Memory Stats ----------------");
    ESP_LOGI(SYS_TAG, "Internal Free: %lu bytes (Min Ever: %lu bytes)", free_internal, min_internal);
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
        ESP_LOGI(SYS_TAG, "PSRAM Free:    %lu bytes", free_psram);
    }

    // --- 2. CPU 任务状态统计 ---
    // 如果没有在 menuconfig 开启宏，这部分将跳过
#if configGENERATE_RUN_TIME_STATS
    ESP_LOGI(SYS_TAG, "---------------- Task Stats ------------------");
    // 动态分配内存给 buffer，因为任务多了字符串会很长
    char *buffer = (char *)malloc(2048);
    if (buffer) {
        vTaskGetRunTimeStats(buffer);
        printf("%s", buffer); // 直接打印格式化好的表格
        free(buffer);
    }
#else
    ESP_LOGW(SYS_TAG, "Enable configGENERATE_RUN_TIME_STATS in menuconfig to see CPU usage.");
#endif

    // --- 3. 栈深度监控 (当前调用者的栈) ---
    ESP_LOGI(SYS_TAG, "Current Task Stack High Water Mark: %u bytes", uxTaskGetStackHighWaterMark(NULL));
    ESP_LOGI(SYS_TAG, "----------------------------------------------");
}

extern "C" void app_main()
{
    //  创建bno055对象以及相关任务
    auto bno055 = std::make_shared<Bno055Driver>();
    bno055->init();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(bno055);
    auto bno055_read_liner_acc_z_task = std::make_unique<Bno055ReadLinerAccZTask>(bno055);
    // 创建两个led对象，以及相关任务
    std::vector<std::shared_ptr<LED>> led_list{std::make_shared<LED>(LED_GREEN), std::make_shared<LED>(LED_RED)};
    led_list[0]->ledc_init();
    auto g_led_task = std::make_unique<LEDTask>(led_list[0]);
    auto r_led_task = std::make_unique<LEDTask>(led_list[1]);

    // 创建DSP引擎对象以及相关任务
    auto dsp_engine = std::make_shared<DSPEngine>(bno055);
    // 创建MQTT对象和相关任务
    auto mqtt_client = std::make_shared<MQTTClient>();
    auto mqtt_task = std::make_shared<MQTTTask>(mqtt_client, bno055);
    auto mqtt_notify_start_task = std::make_shared<MQTTNotifyStartTask>(mqtt_client);
    auto mqtt_notify_stop_task = std::make_shared<MQTTNotifyStopTask>(mqtt_client);
    auto mqtt_subscribe_task = std::make_shared<MQTTSubscribeTask>(mqtt_client, bno055, led_list);
    // 创建Wifi对象以及相关任务
    auto wifi_station = std::make_unique<WifiStation>(mqtt_task, mqtt_notify_start_task, mqtt_notify_stop_task);
    auto wifi_task = std::make_unique<WifiTask>(std::move(wifi_station));

    // 任务启动
    bno055_read_euler_task->start();
    bno055_read_liner_acc_z_task->start();

    g_led_task->start();
    r_led_task->start();

    wifi_task->start();

    mqtt_task->start();
    mqtt_notify_start_task->start();
    mqtt_notify_stop_task->start();
    mqtt_subscribe_task->start();

    dsp_engine->start();

    while (1) {
#ifdef DEBUG
        dump_system_status();
        vTaskDelay(pdMS_TO_TICKS(10000));
#else
        vTaskDelay(pdMS_TO_TICKS(60000));
#endif
    }
}
