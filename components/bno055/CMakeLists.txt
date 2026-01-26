idf_component_register(SRCS "bno055.c" "bno055.cpp"
                    INCLUDE_DIRS "include" "../../main"
                    REQUIRES esp_driver_i2c Core
                    )