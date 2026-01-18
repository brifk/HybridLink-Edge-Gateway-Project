#include "bno055driver.hpp"

i2c_master_dev_handle_t Bno055Driver::i2c_master_dev_handle = nullptr;
i2c_master_bus_handle_t Bno055Driver::i2c_master_bus_handle = nullptr;