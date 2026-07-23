#include "soil_moisture_breakout_board.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

// Calibration state
static uint16_t dry_raw_calibration = SOIL_DRY_RAW_DEFAULT;
static uint16_t wet_raw_calibration = SOIL_WET_RAW_DEFAULT;

void soil_moisture_init(void) {
    adc_init(); // Initialize the ADC hardware
    adc_gpio_init(SOIL_ADC_GPIO); // Initialize the GPIO pin for ADC
    adc_select_input(SOIL_ADC_CHANNEL); // Select the ADC channel for the soil moisture sensor
}

uint16_t soil_moisture_read_raw(void) {
    adc_select_input(SOIL_ADC_CHANNEL); // Ensure the correct ADC channel is selected
    return adc_read(); // Read the raw ADC value
}

float soil_moisture_raw_to_voltage(uint16_t raw) {
    return ((float)raw / ADC_MAX_COUNTS) * ADC_REF_VOLTAGE; // Convert raw ADC value to voltage
}

float soil_moisture_raw_to_percentage(uint16_t raw) {
    float percent = ((float)dry_raw_calibration - (float)raw) / ((float)dry_raw_calibration - (float)wet_raw_calibration) * 100.0f;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    return percent; // Convert raw ADC value to percentage based on calibration
}

void soil_moisture_calibrate(uint16_t dry_raw, uint16_t wet_raw) {
    dry_raw_calibration = dry_raw; // Update the dry calibration value
    wet_raw_calibration = wet_raw; // Update the wet calibration value  
}

