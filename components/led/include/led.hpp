#pragma once

#include "esp_log.h"
#include "led.h"
#include <string>

class LED {
public:
    LED(led_color_t led_color)
        : m_led_color(led_color) { };
    ~LED() { };
    void ledc_init();
    void init();
    void set(led_state_t state);
    led_info_t* get_led_info();

private:
    static constexpr auto TAG = "LED";
    led_color_t m_led_color;
    std::string led_color_to_string(led_color_t led_color);
    std::string led_state_to_string(led_state_t led_state);
};