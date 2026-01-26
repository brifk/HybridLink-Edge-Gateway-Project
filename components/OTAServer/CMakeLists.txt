idf_component_register(SRCS "OTAServer.cpp"
                       PRIV_REQUIRES esp_http_client 
                                     app_update 
                                     esp_https_ota 
                                     esp_netif 
                                     mbedtls 
                                     nvs_flash 
                                     esp_wifi 
                                     esp_psram 
                                     Core
                       INCLUDE_DIRS "include")