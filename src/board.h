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