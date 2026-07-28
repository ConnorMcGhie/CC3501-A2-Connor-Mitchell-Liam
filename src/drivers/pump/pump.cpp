#include "pump.h"
#include "board.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
 
namespace {
constexpr uint16_t kPwmWrap = 999;  // 1000 duty steps (0-999)
uint s_slice;
uint s_chan;
}  
 
void pump_init() {
    gpio_init(PUMP_DIR_PIN);
    gpio_set_dir(PUMP_DIR_PIN, GPIO_OUT);
    gpio_put(PUMP_DIR_PIN, 0);
 
    gpio_set_function(PUMP_PWM_PIN, GPIO_FUNC_PWM);
    s_slice = pwm_gpio_to_slice_num(PUMP_PWM_PIN);
    s_chan = pwm_gpio_to_channel(PUMP_PWM_PIN);
 
    pwm_set_wrap(s_slice, kPwmWrap);
    pwm_set_chan_level(s_slice, s_chan, 0);  // start stopped
    pwm_set_enabled(s_slice, true);
}
 
void pump_on() {
    pwm_set_chan_level(s_slice, s_chan, kPwmWrap);
}
 
void pump_off() {
    pwm_set_chan_level(s_slice, s_chan, 0);
}
 
void pump_set_speed(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint16_t level = static_cast<uint16_t>((kPwmWrap * percent) / 100);
    pwm_set_chan_level(s_slice, s_chan, level);
}