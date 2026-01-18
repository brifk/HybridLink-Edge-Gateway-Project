#include <stdio.h>
#include "Thread.hpp"
#include "APPConfig.h"
#include "OTAService.hpp"
#include "bno055driver.hpp"
#include "esp_log.h"

const char* TAG = "main";

extern "C" void app_main(void)
{
    Bno055Driver bno055;
    bno055.init();
    ESP_LOGI(TAG, "bno055 init success");
}
