extern "C" {
#include "led.h"
}

class LED {
public:
    LED(led_color_t led_color) : m_led_color(led_color) {};
    ~LED() {};
    void init() {
        led_init();
    };
    void set(led_state_t state) {
        led_set_state(m_led_color, state);
    };
private:
    led_color_t m_led_color;
};