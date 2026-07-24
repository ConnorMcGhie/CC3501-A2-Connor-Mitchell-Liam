#pragma once
#include "hardware/spi.h"

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
#define SOIL_DRY_RAW_DEFAULT 3340 // Fully dry recorded value
#define SOIL_WET_RAW_DEFAULT 1230 // Fully wet recorded value 
#define SENSOR_READ_INTERVAL_MS 2000 // Sensor reads every 2 seconds
#define SOIL_POWER_GPIO 25 // GPIO pin to control power to the soil moisture sensor 
#define SOIL_POWER_STABILIZE_MS 100 // Time to wait for the sensor to stabilise after powering on

// SPI Configuration for SD Card (TF-015)
#define SD_SPI_PORT spi0
#define SD_PIN_SCK 2
#define SD_PIN_MOSI 3
#define SD_PIN_MISO 4
#define SD_PIN_CS 5
#define SD_PIN_CD 6 // Card Detect pin 
#define SD_INIT_HZ (400 * 1000) // 400 kHz for initialization
#define SD_OP_HZ (12 * 1000 * 1000) // 12 MHz for normal operation
#define SD_LOG_START_BLOCK 20000 // Raw block number where logging starts
