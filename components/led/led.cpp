#include "led.hpp"

void LED::ledc_init()
{
    static bool ledc_initialized = false;
    if (ledc_initialized) {
        return;
    }
    ledc_initialized = true;
    // 1. 配置 LEDC 定时器 (只需要配置一次)
    static ledc_timer_config_t ledc_timer = { };
    ledc_timer.duty_resolution = LEDC_DUTY_RES_SEL; // 13-bit resolution
    ledc_timer.freq_hz = LEDC_FREQUENCY_HZ; // 4kHz frequency
    ledc_timer.speed_mode = LEDC_MODE_SEL; // Low Speed Mode
    ledc_timer.timer_num = LEDC_TIMER_SEL;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    // 2. 初始化 LEDC 渐变服务
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    ledc_initialized = true;
    ESP_LOGI(TAG, "LEDC Timer and Fade Function Installed");
}

void LED::init()
{
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
}

void LED::set(led_state_t state)
{
    led_set_state(m_led_color, state);
    ESP_LOGI(TAG, "LED %s set to %s", led_color_to_string(m_led_color).c_str(), led_state_to_string(state).c_str());
}

led_info_t* LED::get_led_info()
{
    return led_get_info(m_led_color);
}

std::string LED::led_color_to_string(led_color_t led_color)
{
    switch (led_color) {
    case LED_GREEN:
        return "GREEN";
    case LED_RED:
        return "RED";
    default:
        return "UNKNOWN";
    }
}

std::string LED::led_state_to_string(led_state_t led_state)
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
}

// ------------------ C 层实现（从原 led.c 合并） ------------------

static const char* TAG = "LED_DRIVER";

led_info_t led_array[] = {
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

static const size_t num_leds = sizeof(led_array) / sizeof(led_array[0]);

// 返回底层 led_array 的指针，确保单一状态来源
led_info_t* led_get_info(led_color_t led_color)
{
    if ((size_t)led_color >= num_leds)
        return NULL;
    return &led_array[led_color];
}

void led_set_state(led_color_t led_color, led_state_t state)
{
    if ((size_t)led_color >= num_leds)
        return;

    led_info_t* led = &led_array[led_color];

    if (led->control_task_handle != NULL) {
        if (state == LED_STATE_ON || state == LED_STATE_OFF || (state != led->state && (led->state == LED_STATE_BLINK_SLOW || led->state == LED_STATE_BLINK_FAST || led->state == LED_STATE_BLINK_DOUBLE || led->state == LED_STATE_BREATH))) {
            vTaskDelete(led->control_task_handle);
            led->control_task_handle = NULL;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    led->state = state;

    switch (state) {
    case LED_STATE_OFF:
        ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
        ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        break;

    case LED_STATE_ON:
        ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
        ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        break;

    case LED_STATE_BLINK_SLOW:
    case LED_STATE_BLINK_FAST:
    case LED_STATE_BLINK_DOUBLE:
    case LED_STATE_BREATH:
        // if (led->control_task_handle == NULL) {
        //     xTaskCreate(led_control_task, "led_control", 3072, (void*)led, 5, &led->control_task_handle);
        // }
        break;
    }
}

void led_set_device_status(device_led_status_t status)
{
    ESP_LOGI(TAG, "Setting Device Status: %d", status);

    led_set_state(LED_GREEN, LED_STATE_OFF);
    led_set_state(LED_RED, LED_STATE_OFF);

    switch (status) {
    case LED_STATUS_SYS_ERROR:
        led_set_state(LED_RED, LED_STATE_BLINK_FAST);
        break;

    case LED_STATUS_CONFIG_WAIT:
        led_set_state(LED_RED, LED_STATE_ON);
        break;

    case LED_STATUS_NETWORK_CONNECTING:
        led_set_state(LED_GREEN, LED_STATE_BLINK_SLOW);
        break;

    case LED_STATUS_NETWORK_FAILED:
        led_set_state(LED_RED, LED_STATE_BLINK_DOUBLE);
        break;

    case LED_STATUS_ONLINE_RUNNING:
        led_set_state(LED_GREEN, LED_STATE_ON);
        break;

    case LED_STATUS_LOW_BATTERY_WARNING:
        led_set_state(LED_GREEN, LED_STATE_ON);
        led_set_state(LED_RED, LED_STATE_BREATH);
        break;

    case LED_STATUS_CHARGING:
        led_set_state(LED_GREEN, LED_STATE_BREATH);
        break;

    case LED_STATUS_CHARGE_COMPLETE:
        led_set_state(LED_GREEN, LED_STATE_ON);
        break;

    case LED_STATUS_CRITICAL_SHUTDOWN:
        led_set_state(LED_RED, LED_STATE_ON);
        ESP_LOGW(TAG, "CRITICAL SHUTDOWN initiated: Red LED ON for 2s. System halt simulated after.");
        vTaskDelay(pdMS_TO_TICKS(2000));
        led_set_state(LED_RED, LED_STATE_OFF);
        ESP_LOGW(TAG, "Red LED OFF. Simulating system power-off now.");
        break;

    default:
        ESP_LOGE(TAG, "Unknown LED status command: %d", status);
        break;
    }
}
