#include "APPConfig.h"
#include "OTAService.hpp"
#include "Thread.hpp"
#include "bno055driver.hpp"
#include "esp_log.h"
#include "led.hpp"
#include <stdio.h>

#define TAG "main"

extern "C" void app_main()
{
    Bno055Driver bno055;
    bno055.init();
    ESP_LOGI(TAG, "bno055 init success");
    LED led;
    led.init();
    led.set(LED_GREEN, LED_STATE_ON);
}
