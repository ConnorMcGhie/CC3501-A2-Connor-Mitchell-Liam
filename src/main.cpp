#include <stdio.h>
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
 
#include "WS2812.pio.h"
#include "board.h"
#include "drivers/logging/logging.h"
#include "drivers/leds/leds.h"
#include "drivers/HTU21D/HTU21D.h"
#include "drivers/BH1750FVI-TR/BH1750FVI-TR.h"
#include "drivers/soil_moisture_breakout_board/soil_moisture_breakout_board.h"

// namespace {
// // Arbitrary demo-only scale for the light gradient LED - not a real
// // "enough light for a plant" threshold, just a range that gives a
// // visible red->green sweep under normal indoor lighting for this test.
// constexpr float kDemoMinLux = 0.0f;
// constexpr float kDemoMaxLux = 1000.0f;
 
// // Maps a lux reading onto a red (dark) -> green (bright) gradient by
// // interpolating hue between 0 (red) and 120 (green) in HSV space -
// // smoother/more intuitive than a linear RGB blend.
// HSV luxToGradient(float lux)
// {
//     float fraction = (lux - kDemoMinLux) / (kDemoMaxLux - kDemoMinLux);
//     if (fraction < 0.0f) fraction = 0.0f;
//     if (fraction > 1.0f) fraction = 1.0f;
 
//     HSV colour;
//     colour.hue = static_cast<uint16_t>(fraction * 120.0f);
//     colour.sat = 255;
//     colour.val = 255;
//     return colour;
// }
// }  // namespace

// int main()
// {
//     stdio_init_all();
//     // Give USB CDC time to enumerate so early printf() calls aren't lost
//     // before PuTTY (or any other terminal) attaches.
//     sleep_ms(2000);

//     // --- Shared I2C bus setup ---
//     // Owned here at board level rather than inside a driver, since both
//     // HTU21D and BH1750FVI-TR sit on the same physical bus.
//     i2c_init(I2C_PORT, I2C_BAUDRATE);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     // Not enabling internal pull-ups here - the schematic already has
//     // external 4.7k pull-ups to 3v3 on both lines.

//     // --- WS2812 LED chain setup ---
//     PIO pio = pio0;
//     uint sm = 0;
//     uint offset = pio_add_program(pio, &ws2812_program);
//     ws2812_program_init(pio, sm, offset, LED_PIN, 800000, false);
//     LEDDriver leds(pio, sm);

//     ///////////////////////////////////////////////////////////////////////
//     // HTU21D DEMONSTRATION

//     // --- HTU21D init test ---
//     // First LED in the chain reports pass/fail: green if the sensor ACKs
//     // and soft-resets successfully, red if init() fails (wiring/power/
//     // address fault).
//     HTU21D htu21d(I2C_PORT);
//     bool htuOk = htu21d.init();
//     leds.set(0, htuOk ? Colours::GREEN : Colours::RED);
//     printf("HTU21D init: %s\n", htuOk ? "OK" : "FAILED");

//     // --- BH1750 init test ---
//     BH1750 bh1750(I2C_PORT);
//     bool bh1750Ok = bh1750.init();
//     printf("BH1750 init: %s\n", bh1750Ok ? "OK" : "FAILED");
 
//     leds.show();

//     // --- Continuous readout for verifying actual values over PuTTY ---
//     while (true) {
//         if (htuOk) {
//             float tempC = 0.0f;
//             float humidityRH = 0.0f;
//             bool tempOk = htu21d.readTemperature(tempC);
//             bool humOk  = htu21d.readHumidity(humidityRH);
 
//             if (tempOk && humOk) {
//                 printf("Temp: %.2f C   Humidity: %.2f %%RH\n", tempC, humidityRH);
//             } else {
//                 printf("HTU21D read failed\n");
//             }
//         }
 
//         if (bh1750Ok) {
//             float lux = 0.0f;
//             if (bh1750.readLux(lux)) {
//                 printf("Light: %.1f lx\n", lux);
 
//                 // LED 1: arbitrary demo gradient, red (dark) -> green
//                 // (bright). Not a real plant-appropriate-light judgement -
//                 // that logic belongs in a higher-level layer later.
//                 leds.set_hsv(1, luxToGradient(lux));
//                 leds.show();
//             } else {
//                 printf("BH1750 read failed\n");
//             }
//         }
 
//     }
//     ///////////////////////////////////////////////////////////////////////
// }

int main() {
    stdio_init_all();

    soil_moisture_init(); // Initialize the soil moisture sensor

    // Once the raw dry/wet values are known, fill this in:
    // soil_moisture_calibrate(/*dry_raw=*/3050, /*wet_raw=*/1280);

    printf("Soil Moisture Sensor Test Starting...\n");

    while(true) {
        uint16_t raw = soil_moisture_read_raw();
        float voltage = soil_moisture_raw_to_voltage(raw);
        float percentage = soil_moisture_raw_to_percentage(raw);

        printf("Raw: %u, Voltage: %.2f V, Moisture: %.2f%%\n", raw, voltage, percentage);

        sleep_ms(SENSOR_READ_INTERVAL_MS);
    }
    return 0;
}
