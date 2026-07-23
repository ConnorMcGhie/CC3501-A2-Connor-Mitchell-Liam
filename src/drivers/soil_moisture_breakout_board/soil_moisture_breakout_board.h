#pragma once 

#include <stdint.h>
#include "board.h"

// The sensor will output a higher voltage in dry soil and a lower voltage in wet soil

// Initialise the ADC hardware
void soil_moisture_init(void);

// Take a single blocking ADC reading from the soil sensor 
uint16_t soil_moisture_read_raw(void);

// Convert a raw ADC reading to volts (0.0V to 3.3V)
float soil_moisture_raw_to_voltage(uint16_t raw);

// Convert a raw ADC reading to a percentage (0% to 100%)
float soil_moisture_raw_to_percentage(uint16_t raw);

// Update the dry/wet calibration bounds
void soil_moisture_calibrate(uint16_t draw_raw, uint16_t wet_raw);

