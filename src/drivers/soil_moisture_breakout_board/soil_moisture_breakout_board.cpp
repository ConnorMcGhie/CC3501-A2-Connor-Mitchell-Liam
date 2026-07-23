#include "soil_moisture_breakout_board.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Calibration state
static uint16_t dry_raw_calibration = SOIL_DRY_RAW_DEFAULT;
static uint16_t wet_raw_calibration = SOIL_WET_RAW_DEFAULT;

void soil_moisture_init(void) {
    adc_init(); // Initialize the ADC hardware
    adc_gpio_init(SOIL_ADC_GPIO); // Initialize the GPIO pin for ADC
    adc_select_input(SOIL_ADC_CHANNEL); // Select the ADC channel for the soil moisture sensor
    gpio_init(SOIL_POWER_GPIO); // Initialize the GPIO pin for powering the sensor
    gpio_set_dir(SOIL_POWER_GPIO, GPIO_OUT); // Set the power GPIO as output
    gpio_put(SOIL_POWER_GPIO, 0); // Ensure the sensor is powered off initially to avoid current draw
}

uint16_t soil_moisture_read_raw(void) {
    gpio_put(SOIL_POWER_GPIO, 1); // Power on the soil moisture sensor
    sleep_ms(SOIL_POWER_STABILIZE_MS); // Wait for the sensor to stabilize
    adc_select_input(SOIL_ADC_CHANNEL); // Ensure the correct ADC channel is selected
    uint16_t raw = adc_read();
    gpio_put(SOIL_POWER_GPIO, 0); // Power off the soil moisture sensor
    return raw; // Return the raw ADC value
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

