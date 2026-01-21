#pragma once

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "bno055.h"
#include "config.h"
#include "LED.h"
#include "cJSON.h"

void StartMqttClientTask(void* pvParameters);
void StopMqttClientTask(void* pvParameters);
void NotifyStartMqttClientTask(void);
void NotifyStopMqttClientTask(void);