#pragma once

#include "Thread.hpp"
#include "esp_log.h"
#include "led.hpp"
#include <memory>

class LEDTask : public Thread {
public:
    LEDTask(std::vector<std::shared_ptr<LED>> led_list)
        : Thread("LEDTask", 1024 * 3, tskIDLE_PRIORITY + 3, 1)
        , led_list(std::move(led_list)) { };
    ~LEDTask() { };
    void run() override
    {
        for (auto& led : led_list) {
            led->ledc_init();
            led->init();
        }
        while (1) {
            for (auto& led : led_list) {
                led_info_t* led_info = led->get_led_info();
                if (led_info->state == LED_STATE_BLINK_SLOW || led_info->state == LED_STATE_BLINK_FAST) {
                    // 闪烁：使用 set_duty + update_duty，利用 LEDC 驱动 GPIO
                    uint32_t period_ms = led_info->blink_period_ms;

                    // 如果是慢闪/快闪，调整周期
                    if (led_info->state == LED_STATE_BLINK_SLOW)
                        period_ms = 300;
                    if (led_info->state == LED_STATE_BLINK_FAST)
                        period_ms = 100;

                    uint32_t half_period_ticks = period_ms / 2 / portTICK_PERIOD_MS;
                    if (half_period_ticks == 0)
                        half_period_ticks = 1;

                    // ON
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(half_period_ticks);

                    // OFF
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(half_period_ticks);

                } else if (led_info->state == LED_STATE_BLINK_DOUBLE) {
                    // 双闪模式 (例如 P4: 网络连接失败)
                    const int BLINK_DURATION_MS = 100;
                    const int PAUSE_DURATION_MS = 500;
                    uint32_t blink_ticks = BLINK_DURATION_MS / portTICK_PERIOD_MS;
                    uint32_t pause_ticks = PAUSE_DURATION_MS / portTICK_PERIOD_MS;

                    // 闪烁 1
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(blink_ticks);

                    // OFF
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(blink_ticks);

                    // 闪烁 2
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(blink_ticks);

                    // 长灭
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                    vTaskDelay(pause_ticks);

                } else if (led_info->state == LED_STATE_BREATH) {
                    const int FADE_TIME_MS = 1500; // 呼吸灯渐变时间 1.5 秒

                    // 渐亮
                    ledc_set_fade_with_time(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty, FADE_TIME_MS);
                    ledc_fade_start(LEDC_MODE_SEL, led_info->ledc_channel, LEDC_FADE_WAIT_DONE);

                    if (led_info->state != LED_STATE_BREATH)
                        break;

                    // 渐暗
                    ledc_set_fade_with_time(LEDC_MODE_SEL, led_info->ledc_channel, 0, FADE_TIME_MS);
                    ledc_fade_start(LEDC_MODE_SEL, led_info->ledc_channel, LEDC_FADE_WAIT_DONE);

                    if (led_info->state != LED_STATE_BREATH)
                        break;

                    vTaskDelay(pdMS_TO_TICKS(50)); // 短暂暂停，避免 LEDC 错误

                } else if (led_info->state == LED_STATE_ON) {
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                } else if (led_info->state == LED_STATE_OFF) {
                    ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
                    ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                } else {
                    break;
                }
                // ESP_LOGI(TAG, "LEDTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
            }
        }
        // 退出任务前，确保 LEDC 输出为 0
        for (auto& led : led_list) {
            led_info_t* led_info = led->get_led_info();
            ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
            ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
            led_info->control_task_handle = NULL;
        }
        vTaskDelete(NULL);
    };

private:
    static constexpr auto TAG = "LEDTask";
    std::vector<std::shared_ptr<LED>> led_list;
};
