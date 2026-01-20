#include "bno055driver.hpp"
#include "bno055task.hpp"

i2c_master_dev_handle_t Bno055Driver::i2c_master_dev_handle = nullptr;
i2c_master_bus_handle_t Bno055Driver::i2c_master_bus_handle = nullptr;

esp_err_t Bno055Driver::init()
{
    static bool bno055_initialized = false;
    if (bno055_initialized) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_OK;
    }
    bno055_initialized = true;
    i2c_master_init(&i2c_master_bus_handle, &i2c_master_dev_handle); // 初始化i2c总线，挂载bno055
    ESP_LOGI(TAG, "i2c master init success");
    s32 comres = BNO055_ERROR;
    comres = bno055_init(&bno055); // 初始化bno055
    bno055_set_operation_mode(BNO055_OPERATION_MODE_NDOF); // 设置操作模式为NDOF，即直接读寄存器就可以得到数值
    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待稳定
    if (comres != BNO055_SUCCESS) {
        ESP_LOGE(TAG, "BNO055 init failed with error: %d", comres);
        return ESP_FAIL;
    }
    bno055_initialized = true;
    ESP_LOGI(TAG, "bno055 init success");
    return ESP_OK;
}

bno055_euler_double_t Bno055Driver::read_double_euler() {
    bno055_convert_double_euler_hpr_deg(&euler);
    return euler;
}

double Bno055Driver::read_linear_accel_z() {
    bno055_convert_double_linear_accel_z_msq(&linear_accel_z);
    return linear_accel_z;
}

s8 Bno055Driver::bno055read(u8 dev_addr, u8 reg_addr, u8* reg_data, u8 wr_len)
{
    esp_err_t err = i2c_master_transmit_receive(i2c_master_dev_handle, &reg_addr, 1, reg_data, wr_len, I2C_MASTER_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed at register 0x%02X: %s", reg_addr, esp_err_to_name(err));
        return BNO055_ERROR;
    }
    return BNO055_SUCCESS;
}

s8 Bno055Driver::bno055write(u8 dev_addr, u8 reg_addr, u8* reg_data, u8 wr_len)
{
    // 创建一个临时缓冲区来存储寄存器地址和数据
    u8* write_buffer = (u8*)malloc(wr_len + 1);
    if (write_buffer == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return BNO055_ERROR;
    }
    write_buffer[0] = reg_addr;
    memcpy(&write_buffer[1], reg_data, wr_len);
    esp_err_t err = i2c_master_transmit(i2c_master_dev_handle, write_buffer, wr_len + 1, I2C_MASTER_TIMEOUT_MS);
    free(write_buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed at register 0x%02X: %s", reg_addr, esp_err_to_name(err));
        return BNO055_ERROR;
    }
    return BNO055_SUCCESS;
}

void Bno055Driver::delay_func(u32 delay_in_msec)
{
    vTaskDelay(pdMS_TO_TICKS(delay_in_msec));
}

void Bno055Driver::i2c_master_init(i2c_master_bus_handle_t* bus_handle, i2c_master_dev_handle_t* dev_handle)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_MASTER_NUM;
    bus_config.sda_io_num = I2C_MASTER_SDA_IO;
    bus_config.scl_io_num = I2C_MASTER_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = BNO055_I2C_ADDR1;
    dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

