idf_component_register(SRCS "OTAServer.cpp"
                             "UartOTAReceiver.cpp"
                       PRIV_REQUIRES esp_http_client 
                                     app_update 
                                     esp_https_ota 
                                     esp_netif 
                                     mbedtls 
                                     nvs_flash 
                                     esp_wifi 
                                     esp_psram 
                                     driver
                                     freertos
                                     esp_ringbuf
                                     Core
                       INCLUDE_DIRS "include")