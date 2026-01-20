#include "esp_log.h"
#include <stdio.h>
#include <memory>

#include "APPConfig.h"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include "bno055task.hpp"
#include "led.hpp"
#include "ledtask.hpp"

static constexpr char* TAG = "main";

extern "C" void app_main()
{
    auto bno055 = std::make_shared<Bno055Driver>();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(bno055);
    auto bno055_read_liner_acc_z_task = std::make_unique<Bno055ReadLinerAccZTask>(bno055);
    auto red_led = std::make_unique<LED>(LED_RED);
    auto red_led_task = std::make_unique<LEDTask>(std::move(red_led));
    auto green_led = std::make_unique<LED>(LED_GREEN);
    auto green_led_task = std::make_unique<LEDTask>(std::move(green_led));
    bno055_read_euler_task->start();
    bno055_read_liner_acc_z_task->start();
    red_led_task->start();
    green_led_task->start();
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}
