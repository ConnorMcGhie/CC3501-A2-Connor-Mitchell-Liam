#pragma once
#include <cstdint>
 
// Configures the PWM/DIR pins and leaves the pump off.
void pump_init();
 
// Runs the pump at full speed.
void pump_on();
 
// Stops the pump.
void pump_off();
 
// Runs the pump at a given speed, 0-100 (%). 0 is equivalent to pump_off(),
// 100 is equivalent to pump_on().
void pump_set_speed(uint8_t percent);