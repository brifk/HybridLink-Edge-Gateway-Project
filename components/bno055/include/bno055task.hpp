#pragma once
#include "Thread.hpp"
#include "Bno055Driver.hpp"
#include <memory>
#include "esp_log.h"

class Bno055ReadEulerTask : public Thread {
public:
    Bno055ReadEulerTask(std::unique_ptr<Bno055Driver> bno055) : Thread("Bno055ReadEulerTask", 2048, 8, 1), bno055(std::move(bno055)) { };
    ~Bno055ReadEulerTask() { };
    void run() override {
        bno055->init();
        while (true) {
            bno055_euler_double_t euler = bno055->read_double_euler();
            ESP_LOGI(TAG, "euler: %f, %f, %f", euler.h, euler.r, euler.p);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
private:
    std::unique_ptr<Bno055Driver> bno055;
};
