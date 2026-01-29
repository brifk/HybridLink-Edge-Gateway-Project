#pragma once
#include "APPConfig.h"
#include "Bno055Driver.hpp"
#include "Thread.hpp"
#include "esp_log.h"
#include <memory>

class Bno055ReadEulerTask : public Thread {
public:
    Bno055ReadEulerTask(std::shared_ptr<Bno055Driver> bno055)
        : Thread("Bno055ReadEulerTask", 1024 * 3, PRIO_SENSOR, 1)
        , bno055(std::move(bno055)) { };
    ~Bno055ReadEulerTask() { };
    void run() override
    {
        bno055->init();
        TickType_t xLastWakeTime = xTaskGetTickCount();
        bno055->bno055_state = Bno055Driver::bno055_state_t::RUNNING_EULER;
        while (true) {
            if (bno055->bno055_state == Bno055Driver::bno055_state_t::RUNNING_EULER) {
                bno055_euler_double_t euler = bno055->read_double_euler();
                bno055->bno055_euler_queue_push(euler);
                // ESP_LOGI(TAG, "euler: %f, %f, %f", euler.h, euler.r, euler.p);
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
            } else {
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
            }
            // ESP_LOGI(TAG, "Bno055ReadEulerTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    }

private:
    static constexpr auto TAG = "Bno055ReadEulerTask";
    std::shared_ptr<Bno055Driver> bno055;
};

class Bno055ReadLinerAccZTask : public Thread {
public:
    Bno055ReadLinerAccZTask(std::shared_ptr<Bno055Driver> bno055)
        : Thread("Bno055ReadLinerAccZTask", 1024 * 3, PRIO_SENSOR, 1)
        , bno055(bno055) { };
    ~Bno055ReadLinerAccZTask() { };
    void run() override
    {
        bno055->init();
        TickType_t xLastWakeTime = xTaskGetTickCount();
        bno055->bno055_state = Bno055Driver::bno055_state_t::RUNNING_LINEAR_ACCEL_Z;
        while (true) {
            if (bno055->bno055_state == Bno055Driver::bno055_state_t::RUNNING_LINEAR_ACCEL_Z) {
                double linear_acc_z = bno055->read_linear_accel_z();
                // ESP_LOGI(TAG, "linear_acc_z: %f", linear_acc_z);
                bno055->bno055_linear_accel_z_queue_push(linear_acc_z);
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
            } else {
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
            }
            // ESP_LOGI(TAG, "Bno055ReadLinerAccZTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
    }

private:
    static constexpr auto TAG = "Bno055ReadLinerAccZTask";
    std::shared_ptr<Bno055Driver> bno055;
};
