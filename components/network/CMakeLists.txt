set(COMPONENT_REQUIRES "Core"
                       "esp_wifi" 
                       "esp_event"
                       "nvs_flash"
                       "mqtt"
                       "esp_netif"
                       json
                       "bno055"
                       "led"
                       calculate)

idf_component_register(SRCS "WifiStation.cpp" "MQTTClient.cpp"
                    INCLUDE_DIRS "include" "../../main"
                    REQUIRES ${COMPONENT_REQUIRES}
                    )