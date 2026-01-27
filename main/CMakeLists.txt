set(CURRENT_SRCS "main.cpp")
set(CURRENT_REQUIRES "bno055"
                     "led"
                     "Core"
                     "network"
                     "calculate"
                     "OTAServer"
                     "esp_ringbuf"
                     "esp_driver_uart"
                     "app_update"
                    )

idf_component_register(SRCS ${CURRENT_SRCS}
                       PRIV_REQUIRES ${CURRENT_REQUIRES}
                       REQUIRES joltwallet__littlefs
                       INCLUDE_DIRS ".")
