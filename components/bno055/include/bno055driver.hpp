#ifndef BNO055_DRIVER_HPP
#define BNO055_DRIVER_HPP

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

extern "C" {
#include "bno055.h"
}

class Bno055Driver {
public:
    // 构造函数
    Bno055Driver()
    {
        bno055.bus_read = bno055read;
        bno055.bus_write = bno055write;
        bno055.dev_addr = BNO055_I2C_ADDR1;
        bno055.delay_msec = delay_func;
    };

    ~Bno055Driver() { };

    esp_err_t init();
    bno055_euler_double_t read_double_euler();
    double read_linear_accel_z();
    void bno055_euler_queue_push(bno055_euler_double_t euler);
    QueueHandle_t get_euler_queue_handle() { return bno055_euler_queue; }
    QueueHandle_t get_linear_accel_z_queue_handle() { return bno055_linear_accel_z_queue; }
    void bno055_linear_accel_z_queue_push(double linear_accel_z);

private:
    QueueHandle_t bno055_euler_queue = xQueueCreate(256, sizeof(bno055_euler_double_t));
    QueueHandle_t bno055_linear_accel_z_queue = xQueueCreate(256, sizeof(double));
    static SemaphoreHandle_t bno055_mutex;
    static constexpr auto TAG = "bno055";
    struct bno055_t bno055;
    static i2c_master_dev_handle_t i2c_master_dev_handle;
    static i2c_master_bus_handle_t i2c_master_bus_handle;
    double linear_accel_z;
    struct bno055_euler_double_t euler;
    static s8 bno055read(u8 dev_addr, u8 reg_addr, u8* reg_data, u8 wr_len);
    static s8 bno055write(u8 dev_addr, u8 reg_addr, u8* reg_data, u8 wr_len);
    static void delay_func(u32 delay_in_msec);
    static void i2c_master_init(i2c_master_bus_handle_t* bus_handle, i2c_master_dev_handle_t* dev_handle);
};
#endif // BNO055_DRIVER_HPP