#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <driver/gpio.h>
#include "driver/ledc.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// --- LED 配置 ---
#define LED_GREEN_GPIO   GPIO_NUM_20
#define LED_RED_GPIO     GPIO_NUM_21

// --- LEDC 配置 (低速模式) ---
#define LEDC_TIMER_SEL      LEDC_TIMER_0
#define LEDC_MODE_SEL       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES_SEL   LEDC_TIMER_13_BIT // 使用 13 位分辨率 (0 - 8191)
#define LEDC_FREQUENCY_HZ   4000              // 4kHz 频率

#define LEDC_GREEN_CHANNEL  LEDC_CHANNEL_0
#define LEDC_RED_CHANNEL    LEDC_CHANNEL_1

// 定义 LED 状态 (基础模式)
typedef enum {
    LED_STATE_OFF,
    LED_STATE_ON,
    LED_STATE_BLINK_SLOW,  // 慢闪 (300ms 周期)
    LED_STATE_BLINK_FAST,  // 快闪 (100ms 周期)
    LED_STATE_BLINK_DOUBLE, // 双闪 (Blink-Blink-Pause)
    LED_STATE_BREATH       // 呼吸灯
} led_state_t;

// 定义 LED 颜色
typedef enum {
    LED_GREEN,
    LED_RED
} led_color_t;

// --- 设备整体状态 (对应您的 P1-P9 方案) ---
typedef enum {
    LED_STATUS_SYS_ERROR = 1,       // P1: 系统启动/致命错误 (R:快闪, G:灭)
    LED_STATUS_CONFIG_WAIT,         // P2: 配置等待 (R:常亮, G:灭)
    LED_STATUS_NETWORK_CONNECTING,  // P3: 网络连接中 (G:慢闪, R:灭)
    LED_STATUS_NETWORK_FAILED,      // P4: 网络连接失败 (R:双闪, G:灭)
    LED_STATUS_ONLINE_RUNNING,      // P5: 正常运行 (G:常亮, R:灭)
    LED_STATUS_LOW_BATTERY_WARNING, // P6: 低电量警告 (G:常亮, R:呼吸灯)
    LED_STATUS_CHARGING,            // P7: 充电中 (G:呼吸灯, R:灭)
    LED_STATUS_CHARGE_COMPLETE,     // P8: 充电完成 (G:常亮, R:灭)
    LED_STATUS_CRITICAL_SHUTDOWN,   // P9: 危急/关机 (R:常亮3s后熄灭, G:灭) <-- 更新
    LED_STATUS_MAX
} device_led_status_t;

// 结构体用于存储每个 LED 的状态和配置
typedef struct {
    gpio_num_t gpio_num;
    ledc_channel_t ledc_channel;
    led_state_t state;
    uint32_t blink_period_ms;
    TaskHandle_t control_task_handle; // 用于闪烁和呼吸灯的任务
    uint32_t max_duty;                // 满占空比值
} led_info_t;

// 命令队列中的数据类型，改为发送设备整体状态
typedef device_led_status_t led_command_t;

// 外部队列句柄
extern QueueHandle_t led_command_queue;

/**
 * @brief 初始化 LED 驱动，包括 LEDC 定时器和通道。
 */
void led_init(void);

/**
 * @brief 设置 LED 基础模式状态。
 *
 * @param led_color 要控制的 LED
 * @param state 要设置的状态 (LED_STATE_OFF, LED_STATE_ON, LED_STATE_BLINK_FAST, etc.)
 */
void led_set_state(led_color_t led_color, led_state_t state);

/**
 * @brief 核心状态机：根据整体设备状态设置红绿灯的组合模式。
 *
 * @param status 设备整体状态枚举值
 */
void led_set_device_status(device_led_status_t status);

void led_control_task(void *arg);

#endif // LED_CONTROL_H