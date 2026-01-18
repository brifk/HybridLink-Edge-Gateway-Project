#include "LED.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

// FreeRTOS 任务：处理闪烁和呼吸灯的控制逻辑
void led_control_task(void* arg)
{
    led_info_t* led = (led_info_t*)arg;

    // 初始化 LEDC 通道配置 (确保任务启动时 LEDC 通道配置正确)
    ledc_channel_config_t ledc_channel_cfg = {
        .gpio_num = led->gpio_num,
        .speed_mode = LEDC_MODE_SEL,
        .channel = led->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_SEL,
        .duty = 0, // 初始占空比为 0
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel_cfg);

    while (1) {
        if (led->state == LED_STATE_BLINK_SLOW || led->state == LED_STATE_BLINK_FAST) {
            // 闪烁：使用 set_duty + update_duty，利用 LEDC 驱动 GPIO
            uint32_t period_ms = led->blink_period_ms;

            // 如果是慢闪/快闪，调整周期
            if (led->state == LED_STATE_BLINK_SLOW)
                period_ms = 300;
            if (led->state == LED_STATE_BLINK_FAST)
                period_ms = 100;

            uint32_t half_period_ticks = period_ms / 2 / portTICK_PERIOD_MS;
            if (half_period_ticks == 0)
                half_period_ticks = 1;

            // ON
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(half_period_ticks);

            // OFF
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(half_period_ticks);

        } else if (led->state == LED_STATE_BLINK_DOUBLE) {
            // 双闪模式 (例如 P4: 网络连接失败)
            const int BLINK_DURATION_MS = 100;
            const int PAUSE_DURATION_MS = 500;
            uint32_t blink_ticks = BLINK_DURATION_MS / portTICK_PERIOD_MS;
            uint32_t pause_ticks = PAUSE_DURATION_MS / portTICK_PERIOD_MS;

            // 闪烁 1
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(blink_ticks);

            // OFF
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(blink_ticks);

            // 闪烁 2
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(blink_ticks);

            // 长灭
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
            vTaskDelay(pause_ticks);

        } else if (led->state == LED_STATE_BREATH) {
            const int FADE_TIME_MS = 1500; // 呼吸灯渐变时间 1.5 秒

            // 渐亮
            ledc_set_fade_with_time(LEDC_MODE_SEL, led->ledc_channel, led->max_duty, FADE_TIME_MS);
            ledc_fade_start(LEDC_MODE_SEL, led->ledc_channel, LEDC_FADE_WAIT_DONE);

            if (led->state != LED_STATE_BREATH)
                break;

            // 渐暗
            ledc_set_fade_with_time(LEDC_MODE_SEL, led->ledc_channel, 0, FADE_TIME_MS);
            ledc_fade_start(LEDC_MODE_SEL, led->ledc_channel, LEDC_FADE_WAIT_DONE);

            if (led->state != LED_STATE_BREATH)
                break;

            vTaskDelay(pdMS_TO_TICKS(50)); // 短暂暂停，避免 LEDC 错误

        } else if (led->state == LED_STATE_ON) {
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        } else if (led->state == LED_STATE_ON) {
            ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
            ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        } else {
            break;
        }
    }

    // 退出任务前，确保 LEDC 输出为 0
    ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
    ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);

    led->control_task_handle = NULL;
    vTaskDelete(NULL);
}

void led_init(void)
{
    ESP_LOGI(TAG, "LED Driver Initialization (LEDC)");

    // 1. 配置 LEDC 定时器 (只需要配置一次)
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES_SEL, // 13-bit resolution
        .freq_hz = LEDC_FREQUENCY_HZ, // 4kHz frequency
        .speed_mode = LEDC_MODE_SEL, // Low Speed Mode
        .timer_num = LEDC_TIMER_SEL,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. 初始化 LEDC 渐变服务
    ESP_ERROR_CHECK(ledc_fade_func_install(0));

    // 3. 初始配置 GPIO
    for (size_t i = 0; i < num_leds; i++) {
        ledc_channel_config_t ledc_channel_cfg = {
            .gpio_num = led_array[i].gpio_num,
            .speed_mode = LEDC_MODE_SEL,
            .channel = led_array[i].ledc_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_SEL,
            .duty = 0, // 初始占空比为 0 (LED OFF)
            .hpoint = 0
        };
        ledc_channel_config(&ledc_channel_cfg);
    }
}

void led_set_state(led_color_t led_color, led_state_t state)
{
    if (led_color >= num_leds)
        return;

    led_info_t* led = &led_array[led_color];

    // 1. 停止正在运行的任务 (只有在状态改变且是 BLINK/BREATH 时才需要)
    if (led->control_task_handle != NULL) {
        // 如果新状态还是 BLINK/BREATH，并且模式一样，可以考虑不停止，但这里为了确保参数刷新，选择停止
        if (state == LED_STATE_ON || state == LED_STATE_OFF || (state != led->state && (led->state == LED_STATE_BLINK_SLOW || led->state == LED_STATE_BLINK_FAST || led->state == LED_STATE_BLINK_DOUBLE || led->state == LED_STATE_BREATH))) {
            vTaskDelete(led->control_task_handle);
            led->control_task_handle = NULL;
            // 短暂延时等待任务清理
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // 2. 更新状态
    led->state = state;

    // 3. 根据新状态执行操作
    switch (state) {
    case LED_STATE_OFF:
        // 直接将占空比设为 0 (LED OFF)
        ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, 0);
        ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        break;

    case LED_STATE_ON:
        // 直接将占空比设为最大值 (LED ON)
        ledc_set_duty(LEDC_MODE_SEL, led->ledc_channel, led->max_duty);
        ledc_update_duty(LEDC_MODE_SEL, led->ledc_channel);
        break;

    case LED_STATE_BLINK_SLOW:
    case LED_STATE_BLINK_FAST:
    case LED_STATE_BLINK_DOUBLE:
    case LED_STATE_BREATH:
        // 启动任务处理复杂逻辑 (闪烁或呼吸灯)
        if (led->control_task_handle == NULL) {
            xTaskCreate(led_control_task, "led_control", 3072, (void*)led, 5, &led->control_task_handle);
        }
        break;
    }
}

// --- 核心状态机函数 ---
void led_set_device_status(device_led_status_t status)
{
    ESP_LOGI(TAG, "Setting Device Status: %d", status);

    // 统一将所有 LED 关闭以清除旧状态的任务
    // 注意： led_set_state 内部有逻辑确保不会重复创建任务
    led_set_state(LED_GREEN, LED_STATE_OFF);
    led_set_state(LED_RED, LED_STATE_OFF);

    switch (status) {
    case LED_STATUS_SYS_ERROR: // P1: 系统启动/致命错误 (R:快闪, G:灭)
        led_set_state(LED_RED, LED_STATE_BLINK_FAST);
        break;

    case LED_STATUS_CONFIG_WAIT: // P2: 配置等待 (R:常亮, G:灭)
        led_set_state(LED_RED, LED_STATE_ON);
        break;

    case LED_STATUS_NETWORK_CONNECTING: // P3: 网络连接中 (G:慢闪, R:灭)
        led_set_state(LED_GREEN, LED_STATE_BLINK_SLOW);
        break;

    case LED_STATUS_NETWORK_FAILED: // P4: 网络连接失败 (R:双闪, G:灭)
        led_set_state(LED_RED, LED_STATE_BLINK_DOUBLE);
        break;

    case LED_STATUS_ONLINE_RUNNING: // P5: 正常运行 (G:常亮, R:灭)
        led_set_state(LED_GREEN, LED_STATE_ON);
        break;

    case LED_STATUS_LOW_BATTERY_WARNING: // P6: 低电量警告 (G:常亮, R:呼吸灯)
        led_set_state(LED_GREEN, LED_STATE_ON);
        led_set_state(LED_RED, LED_STATE_BREATH);
        break;

    case LED_STATUS_CHARGING: // P7: 充电中 (G:呼吸灯, R:灭)
        led_set_state(LED_GREEN, LED_STATE_BREATH);
        break;

    case LED_STATUS_CHARGE_COMPLETE: // P8: 充电完成 (G:常亮, R:灭)
        led_set_state(LED_GREEN, LED_STATE_ON);
        break;

    case LED_STATUS_CRITICAL_SHUTDOWN: // P9: 危急/关机 (R:常亮3s后熄灭, G:灭) <-- 修改点
        led_set_state(LED_RED, LED_STATE_ON);
        ESP_LOGW(TAG, "CRITICAL SHUTDOWN initiated: Red LED ON for 2s. System halt simulated after.");

        // 延时 2 秒作为关机警告
        // 注意: 此处会阻塞 LED_control_task 2秒，之后 LEDC 驱动的红灯会被显式关闭。
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 关机前最后一步：熄灭红灯 (模拟系统断电/Deep Sleep)
        led_set_state(LED_RED, LED_STATE_OFF);
        // 实际应用中，这里会紧接着调用关机函数，如 esp_deep_sleep_start()
        ESP_LOGW(TAG, "Red LED OFF. Simulating system power-off now.");
        break;

    default:
        ESP_LOGE(TAG, "Unknown LED status command: %d", status);
        break;
    }
}