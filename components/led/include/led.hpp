#pragma once

#include "esp_log.h"
#include "led.h"

class LED {
public:
    LED(led_color_t led_color)
        : m_led_color(led_color) { };
    ~LED() { };

    void init()
    {
        ESP_LOGI(TAG, "LED Driver Initialization (LEDC)");

        // 1. 配置 LEDC 定时器 (只需要配置一次)
        ledc_timer_config_t ledc_timer;
        ledc_timer.duty_resolution = LEDC_DUTY_RES_SEL; // 13-bit resolution
        ledc_timer.freq_hz = LEDC_FREQUENCY_HZ; // 4kHz frequency
        ledc_timer.speed_mode = LEDC_MODE_SEL; // Low Speed Mode
        ledc_timer.timer_num = LEDC_TIMER_SEL;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;

        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        // 2. 初始化 LEDC 渐变服务
        ESP_ERROR_CHECK(ledc_fade_func_install(0));
        led_info_t* led_info = get_led_info();

        // 初始化 LEDC 通道配置 (确保任务启动时 LEDC 通道配置正确)
        ledc_channel_config_t ledc_channel_cfg;
        ledc_channel_cfg.gpio_num = led_info->gpio_num;
        ledc_channel_cfg.speed_mode = LEDC_MODE_SEL;
        ledc_channel_cfg.channel = led_info->ledc_channel;
        ledc_channel_cfg.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_cfg.timer_sel = LEDC_TIMER_SEL;
        ledc_channel_cfg.duty = 0; // 初始占空比为 0
        ledc_channel_cfg.hpoint = 0;

        ledc_channel_config(&ledc_channel_cfg);
        ESP_LOGI(TAG, "LED %s init", led_color_to_string(m_led_color).c_str());
    };

    void set(led_state_t state)
    {
        led_set_state(m_led_color, state);
        ESP_LOGI(TAG, "LED %s set to %s", led_color_to_string(m_led_color).c_str(), led_state_to_string(state).c_str());
    };

    led_info_t* get_led_info()
    {
        static led_info_t local_led_array[2] = {
            { .gpio_num = LED_GREEN_GPIO,
                .ledc_channel = LEDC_GREEN_CHANNEL,
                .state = LED_STATE_BLINK_SLOW,
                .blink_period_ms = 500, // 默认慢闪周期 (500ms)
                .control_task_handle = NULL,
                .max_duty = (1 << LEDC_DUTY_RES_SEL) - 1 },
            { .gpio_num = LED_RED_GPIO,
                .ledc_channel = LEDC_RED_CHANNEL,
                .state = LED_STATE_BLINK_SLOW,
                .blink_period_ms = 500, // 默认慢闪周期 (500ms)
                .control_task_handle = NULL,
                .max_duty = (1 << LEDC_DUTY_RES_SEL) - 1 }
        };
        return &local_led_array[m_led_color];
    };

private:
    static constexpr char* TAG = "LED";
    led_color_t m_led_color;

    std::string led_color_to_string(led_color_t led_color)
    {
        switch (led_color) {
        case LED_GREEN:
            return "GREEN";
        case LED_RED:
            return "RED";
        default:
            return "UNKNOWN";
        }
    };

    std::string led_state_to_string(led_state_t led_state)
    {
        switch (led_state) {
        case LED_STATE_ON:
            return "ON";
        case LED_STATE_OFF:
            return "OFF";
        case LED_STATE_BLINK_SLOW:
            return "BLINK_SLOW";
        case LED_STATE_BLINK_FAST:
            return "BLINK_FAST";
        case LED_STATE_BLINK_DOUBLE:
            return "BLINK_DOUBLE";
        case LED_STATE_BREATH:
            return "BREATH";
        default:
            return "UNKNOWN";
        }
    };
};