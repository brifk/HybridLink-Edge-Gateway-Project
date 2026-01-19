#include "APPConfig.h"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include "bno055task.hpp"
#include "esp_log.h"
#include "led.hpp"
#include <stdio.h>
#include <memory>

#define TAG "main"

extern "C" void app_main()
{
    auto bno055 = std::make_unique<Bno055Driver>();
    auto bno055_read_euler_task = std::make_unique<Bno055ReadEulerTask>(std::move(bno055));
    bno055_read_euler_task->start();
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}
