#pragma once

#define SSID "R9000P"
// #define SSID "orangepi"
#define PASSWORD "12345678"

#define MQTT_BROKER_URL "mqtt://192.168.137.1:1883"

#define PRIO_SENSOR   tskIDLE_PRIORITY + 10
#define PRIO_WIFI     tskIDLE_PRIORITY + 6
#define PRIO_MQTT     tskIDLE_PRIORITY + 5
#define PRIO_FFT      tskIDLE_PRIORITY + 4
#define PRIO_LED      tskIDLE_PRIORITY + 1