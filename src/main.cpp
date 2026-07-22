#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"

#include "WS2812.pio.h"
#include "board.h"
#include "drivers/logging/logging.h"
#include "drivers/leds/leds.h"
#include "drivers/HTU21D/HTU21D.h"

int main()
{
    stdio_init_all();
    // Give USB CDC time to enumerate so early printf() calls aren't lost
    // before PuTTY (or any other terminal) attaches.
    sleep_ms(2000);

    // --- Shared I2C bus setup ---
    // Owned here at board level rather than inside a driver, since both
    // HTU21D and BH1750FVI-TR sit on the same physical bus.
    i2c_init(I2C_PORT, I2C_BAUDRATE);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    // Not enabling internal pull-ups here - the schematic already has
    // external 4.7k pull-ups to 3v3 on both lines.

    // --- WS2812 LED chain setup ---
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, false);
    LEDDriver leds(pio, sm);

    ///////////////////////////////////////////////////////////////////////
    // HTU21D DEMONSTRATION

    // --- HTU21D init test ---
    // First LED in the chain reports pass/fail: green if the sensor ACKs
    // and soft-resets successfully, red if init() fails (wiring/power/
    // address fault).
    HTU21D htu21d(I2C_PORT);
    bool htuOk = htu21d.init();

    leds.set(0, htuOk ? Colours::GREEN : Colours::RED);
    leds.show();

    printf("HTU21D init: %s\n", htuOk ? "OK" : "FAILED");

    // --- Continuous readout for verifying actual values over PuTTY ---
    while (true) {
        if (htuOk) {
            float tempC = 0.0f;
            float humidityRH = 0.0f;
            bool tempOk = htu21d.readTemperature(tempC);
            bool humOk  = htu21d.readHumidity(humidityRH);

            if (tempOk && humOk) {
                printf("Temp: %.2f C   Humidity: %.2f %%RH\n", tempC, humidityRH);
            } else {
                printf("HTU21D read failed\n");
            }
        }
        sleep_ms(1000);
    }
    ///////////////////////////////////////////////////////////////////////
}