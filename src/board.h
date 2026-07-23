#pragma once

// --- WS2812 LED chain configuration ---
#define LED_PIN 14
#define NUM_LEDS 12

// --- I2C bus configuration ---
// Shared bus for HTU21D + BH1750FVI-TR
#define I2C_PORT     i2c0
#define I2C_SDA_PIN  16
#define I2C_SCL_PIN  17
#define I2C_BAUDRATE 400000  // 400kHz - within spec for both HTU21D and BH1750FVI-TR

// Soil Moisture Breakout Board 
#define SOIL_ADC_GPIO 26
#define SOIL_ADC_CHANNEL 0
#define ADC_REF_VOLTAGE 3.3f
#define ADC_MAX_COUNTS 4095.0f // 12-bit ADC
#define SOIL_DRY_RAW_DEFAULT 3000 // Replace with own recorded values after
#define SOIL_WET_RAW_DEFAULT 1300 // Replace with own recorded values after 
#define SENSOR_READ_INTERVAL_MS 2000 // Sensor reads every 2 seconds
#define SOIL_POWER_GPIO 25 // GPIO pin to control power to the soil moisture sensor 
#define SOIL_POWER_STABILIZE_MS 100 // Time to wait for the sensor to stabilise after powering on
