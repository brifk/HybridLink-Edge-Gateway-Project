idf_component_register(SRCS "OTAServer.cpp"
                       REQUIRES esp_http_client 
                                app_update 
                                esp_https_ota 
                                esp_netif 
                                mbedtls 
                                nvs_flash 
                                esp_wifi 
                                Core
                       INCLUDE_DIRS "include")