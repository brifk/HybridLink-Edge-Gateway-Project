idf_component_register(SRCS "led.cpp"
                    INCLUDE_DIRS "include" "../../main"
                    REQUIRES esp_driver_gpio esp_driver_ledc
                    )