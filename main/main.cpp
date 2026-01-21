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

static constexpr auto TAG = "main";

extern "C" void app_main()
{
    auto bno055 = std::make_shared<Bno055Driver>();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(bno055);
    auto bno055_read_liner_acc_z_task = std::make_unique<Bno055ReadLinerAccZTask>(bno055);

    auto red_led = std::make_unique<LED>(LED_RED);
    auto green_led = std::make_unique<LED>(LED_GREEN);

    auto red_led_task = std::make_unique<LEDTask>(std::move(red_led));
    auto green_led_task = std::make_unique<LEDTask>(std::move(green_led));

    auto wifi_station = std::make_unique<WifiStation>();
    auto wifi_task = std::make_unique<WifiTask>(std::move(wifi_station));

    bno055_read_euler_task->start();
    bno055_read_liner_acc_z_task->start();
    
    red_led_task->start();
    green_led_task->start();

    wifi_task->start();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}
