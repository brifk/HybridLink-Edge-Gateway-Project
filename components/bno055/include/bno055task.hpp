#pragma once
#include "Thread.hpp"
#include "Bno055Driver.hpp"
#include <memory>
#include "esp_log.h"

static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED; // 定义自旋锁，保护bno055的访问

class Bno055ReadEulerTask : public Thread {
public:
    Bno055ReadEulerTask(std::shared_ptr<Bno055Driver> bno055) : Thread("Bno055ReadEulerTask", 2048, 8, 1), bno055(bno055) { };
    ~Bno055ReadEulerTask() { };
    void run() override {
        bno055->init();
        while (true) {
            taskENTER_CRITICAL(&my_spinlock);
            bno055_euler_double_t euler = bno055->read_double_euler();
            taskEXIT_CRITICAL(&my_spinlock);
            ESP_LOGI(TAG, "euler: %f, %f, %f", euler.h, euler.r, euler.p);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
private:
    static constexpr char* TAG = "Bno055ReadEulerTask";
    std::shared_ptr<Bno055Driver> bno055;
};

class Bno055ReadLinerAccZTask : public Thread {
public:
    Bno055ReadLinerAccZTask(std::shared_ptr<Bno055Driver> bno055) : Thread("Bno055ReadLinerAccZTask", 2048, 8, 1), bno055(bno055) { };
    ~Bno055ReadLinerAccZTask() { };
    void run() override {
        bno055->init();
        while (true) {
            taskENTER_CRITICAL(&my_spinlock);
            double linear_acc_z = bno055->read_linear_accel_z();
            taskEXIT_CRITICAL(&my_spinlock);
            ESP_LOGI(TAG, "linear_acc_z: %f", linear_acc_z);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
private:
    static constexpr char* TAG = "Bno055ReadLinerAccZTask";
    std::shared_ptr<Bno055Driver> bno055;
};
