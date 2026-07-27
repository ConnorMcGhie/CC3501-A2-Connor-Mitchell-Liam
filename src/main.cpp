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
#include "drivers/TF-015/TF-015.h"
#include "drivers/RN4871/RN4871.h"

namespace {
// Demo-only lux->gradient scale (red=dark, green=bright) - not a real
// "enough light for a plant" threshold, just a visible sweep for testing.
constexpr float kDemoMinLux = 0.0f;
constexpr float kDemoMaxLux = 1000.0f;
 
HSV luxToGradient(float lux) {
    float fraction = (lux - kDemoMinLux) / (kDemoMaxLux - kDemoMinLux);
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
 
    HSV colour;
    colour.hue = static_cast<uint16_t>(fraction * 120.0f);
    colour.sat = 255;
    colour.val = 255;
    return colour;
}
}  // namespace
 
int main() {
    stdio_init_all();
    sleep_ms(2000);  // let USB CDC enumerate before early printf()s
 
    // --- Shared I2C bus (external 4.7k pull-ups already on board) ---
    i2c_init(I2C_PORT, I2C_BAUDRATE);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
 
    // --- WS2812 LED chain ---
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, false);
    LEDDriver leds(pio, sm);
 
    // --- Sensor / peripheral init ---
    HTU21D htu21d(I2C_PORT);
    bool htuOk = htu21d.init();
    leds.set(0, htuOk ? Colours::GREEN : Colours::RED);
    printf("HTU21D init: %s\n", htuOk ? "OK" : "FAILED");
 
    BH1750 bh1750(I2C_PORT);
    bool bh1750Ok = bh1750.init();
    printf("BH1750 init: %s\n", bh1750Ok ? "OK" : "FAILED");
 
    soil_moisture_init();
    soil_moisture_calibrate(SOIL_DRY_RAW_DEFAULT, SOIL_WET_RAW_DEFAULT);  // dry=3340, wet=1230
 
    bool sdOk = tf015_init();
    if (!sdOk) {
        printf("SD card init failed\n");
    } else {
        tf015_log_line("raw,voltage_v,moisture_pct,temp_c,humidity_pct,lux"); // Add in headings for columns in csv file
    }

    // --- RN4871 Bluetooth Slave initialization ---
    printf("\nConfiguring BLE Slave on UART channel %d (TX: GPIO %d, RX: GPIO %d) at %d baud...\n", 
           (BLE_UART_ID == uart1 ? 1 : 0), BLE_TX_PIN, BLE_RX_PIN, BLE_BAUD_RATE);
    RN4871Slave ble_slave(BLE_UART_ID, BLE_TX_PIN, BLE_RX_PIN, BLE_BAUD_RATE);
    ble_slave.begin();

    if (ble_slave.enter_command_mode()) {
        ble_slave.setup_slave_mode("SuttoPicoSlave");
    } else {
        printf("Warning: BLE module failed link sync! Check physical pin cross-overs.\n");
    }
 
    leds.show();
    printf("Sensor logging starting...\n");
 
    while (true) {
        // Soil moisture
        uint16_t raw = soil_moisture_read_raw();
        float moistVoltage = soil_moisture_raw_to_voltage(raw);
        float moisturePct = soil_moisture_raw_to_percentage(raw);
        printf("Raw: %u, Voltage: %.2f V, Moisture: %.2f%%\n", raw, moistVoltage, moisturePct);
 
        // Temperature / humidity
        float tempC = 0.0f, humidityRH = 0.0f;
        bool envOk = false;
        if (htuOk) {
            envOk = htu21d.readTemperature(tempC) && htu21d.readHumidity(humidityRH);
            if (envOk) {
                printf("Temp: %.2f C   Humidity: %.2f %%RH\n", tempC, humidityRH);
            } else {
                printf("HTU21D read failed\n");
            }
        }
 
        // Light
        float lux = 0.0f;
        bool luxOk = false;
        if (bh1750Ok) {
            luxOk = bh1750.readLux(lux);
            if (luxOk) {
                printf("Light: %.1f lx\n", lux);
                leds.set_hsv(1, luxToGradient(lux));  // LED1: red(dark) -> green(bright)
                leds.show();
            } else {
                printf("BH1750 read failed\n");
            }
        }
 
        // Log this cycle's readings as one CSV line (-1.00 marks a sensor
        // that failed/isn't present, so the column count stays consistent)
        if (sdOk) {
            char line[128];
            snprintf(line, sizeof(line), "%u,%.2f,%.2f,%.2f,%.2f,%.1f",
                      raw, moistVoltage, moisturePct,
                      envOk ? tempC : -1.0f, envOk ? humidityRH : -1.0f,
                      luxOk ? lux : -1.0f);
            tf015_log_line(line);
        }
 
        sleep_ms(SENSOR_READ_INTERVAL_MS);
        
        // --- Format and Transmit Telemetry Packet over Bluetooth ---
        // Construct a clean, structured string package with terminal carriage returns
        char ble_packet[128];
        snprintf(ble_packet, sizeof(ble_packet), 
                 "DATA: Moist=%.1f%%, Temp=%.1fC, Humid=%.1f%%, Light=%.1flux\r\n",
                 moisturePct, 
                 envOk ? tempC : -1.0f, 
                 envOk ? humidityRH : -1.0f, 
                 luxOk ? lux : -1.0f);
        
        // Transmit the string package through the active BLE data mode stream
        ble_slave.send_string(ble_packet);
        printf("Bluetooth telemetry packet dispatched to Master.\n");\

        // Non-blocking sleep loop: dynamically monitors Bluetooth streaming buffer
        // during your SENSOR_READ_INTERVAL_MS interval.
        uint32_t elapsed_ms = 0;
        while (elapsed_ms < SENSOR_READ_INTERVAL_MS) {
            std::string incoming = ble_slave.read_response(10);
            if (!incoming.empty()) {
                printf("%s", incoming.c_str());
            }
            sleep_ms(10);
            elapsed_ms += 10;
        }
    }
 
    tf015_close();
    return 0;
}