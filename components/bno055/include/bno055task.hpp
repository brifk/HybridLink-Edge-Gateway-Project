#pragma once
#include "Thread.hpp"
#include "Bno055Driver.hpp"
#include <memory>
#include "esp_log.h"

class Bno055ReadEulerTask : public Thread {
public:
    Bno055ReadEulerTask(std::shared_ptr<Bno055Driver> bno055) : Thread("Bno055ReadEulerTask", 1024 * 3, 8, 1), bno055(bno055) { };
    ~Bno055ReadEulerTask() { };
    void run() override {
        bno055->init();
        while (true) {
            bno055_euler_double_t euler = bno055->read_double_euler();
            ESP_LOGI(TAG, "euler: %f, %f, %f", euler.h, euler.r, euler.p);
            vTaskDelay(pdMS_TO_TICKS(500));
            // ESP_LOGI(TAG, "Bno055ReadEulerTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    }
private:
    static constexpr auto TAG = "Bno055ReadEulerTask";
    std::shared_ptr<Bno055Driver> bno055;
};

class Bno055ReadLinerAccZTask : public Thread {
public:
    Bno055ReadLinerAccZTask(std::shared_ptr<Bno055Driver> bno055) : Thread("Bno055ReadLinerAccZTask", 1024 * 3, 8, 1), bno055(bno055) { };
    ~Bno055ReadLinerAccZTask() { };
    void run() override {
        bno055->init();
        while (true) {
            double linear_acc_z = bno055->read_linear_accel_z();
            ESP_LOGI(TAG, "linear_acc_z: %f", linear_acc_z);
            vTaskDelay(pdMS_TO_TICKS(500));
            // ESP_LOGI(TAG, "Bno055ReadLinerAccZTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    }
private:
    static constexpr auto TAG = "Bno055ReadLinerAccZTask";
    std::shared_ptr<Bno055Driver> bno055;
};
