#pragma once

#include "Thread.hpp"
#include "esp_log.h"
#include "led.hpp"
#include <memory>

class LEDTask : public Thread {
public:
    LEDTask(std::shared_ptr<LED> led)
        : Thread("LEDTask", 1024 * 3, PRIO_LED, 1)
        , led(led) { };
    ~LEDTask() { };
    void run() override
    {
        led->init();
        led_info_t* led_info = led->get_led_info();
        while (1) {
            // TODO: 解决两个LED不能同时控制的问题
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

                // 渐亮 - 非阻塞方式
                ledc_set_fade_with_time(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty, FADE_TIME_MS);
                ledc_fade_start(LEDC_MODE_SEL, led_info->ledc_channel, LEDC_FADE_NO_WAIT);

                // 等待渐亮完成，同时定期重置看门狗
                int fade_wait_time = FADE_TIME_MS / 100;  // 每100ms检查一次
                for (int i = 0; i < fade_wait_time && led_info->state == LED_STATE_BREATH; i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                if (led_info->state != LED_STATE_BREATH)
                    break;

                // 渐暗 - 非阻塞方式
                ledc_set_fade_with_time(LEDC_MODE_SEL, led_info->ledc_channel, 0, FADE_TIME_MS);
                ledc_fade_start(LEDC_MODE_SEL, led_info->ledc_channel, LEDC_FADE_NO_WAIT);

                // 等待渐暗完成，同时定期重置看门狗
                for (int i = 0; i < fade_wait_time && led_info->state == LED_STATE_BREATH; i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                if (led_info->state != LED_STATE_BREATH)
                    break;

                vTaskDelay(pdMS_TO_TICKS(50)); // 短暂暂停，避免 LEDC 错误

            } else if (led_info->state == LED_STATE_ON) {
                ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, led_info->max_duty);
                ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                vTaskDelay(pdMS_TO_TICKS(50));
            } else if (led_info->state == LED_STATE_OFF) {
                ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
                ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            // ESP_LOGI(TAG, "LEDTask stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
        }
        ledc_set_duty(LEDC_MODE_SEL, led_info->ledc_channel, 0);
        ledc_update_duty(LEDC_MODE_SEL, led_info->ledc_channel);
        led_info->control_task_handle = NULL;

        vTaskDelete(NULL);
    };

private:
    static constexpr auto TAG = "LEDTask";
    std::shared_ptr<LED> led;
};