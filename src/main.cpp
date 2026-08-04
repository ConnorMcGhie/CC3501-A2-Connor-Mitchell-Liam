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
#include "drivers/pump/pump.h"

namespace {
// Each alert LED sits off until its sensor's reading strays outside the
// healthy range configured in board.h, at which point it flashes red to
// catch the eye. `phase_ms` is a free-running millisecond counter shared
// by all LEDs so their flashes stay in sync.
// Returns true if the LED's staged colour actually changed, so the caller
// only needs to call leds.show() when something is different.
bool updateAlertLED(LEDDriver& leds, uint8_t index, bool alert, uint32_t phase_ms) {
    Colour before = leds.get(index);
 
    if (!alert) {
        leds.set(index, Colours::OFF);
    } else {
        bool flashOn = (phase_ms % (2 * ALERT_FLASH_INTERVAL_MS)) < ALERT_FLASH_INTERVAL_MS;
        leds.set(index, flashOn ? Colours::RED : Colours::OFF);
    }
 
    Colour after = leds.get(index);
    return before.red != after.red || before.green != after.green || before.blue != after.blue;
}

// --- Pump safety toggle button ---
// Pressing SWITCH_BUTTON_PIN toggles pump watering on/off as a manual
// safety lockout. Debounced, toggles on the press (rising) edge only, so
// holding the button down doesn't repeatedly flip the state.
bool pumpSafetyEnabled = true;      // true = pump allowed to run
bool lastButtonRaw = false;
uint32_t lastButtonChangeMs = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;

// Call frequently (e.g. every 10ms tick) so presses are caught promptly.
void pollSafetyButton() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool pressed = gpio_get(SWITCH_BUTTON_PIN);

    if (pressed != lastButtonRaw && (now - lastButtonChangeMs) > BUTTON_DEBOUNCE_MS) {
        lastButtonChangeMs = now;
        lastButtonRaw = pressed;
        if (pressed) {  // toggle on press, not release
            pumpSafetyEnabled = !pumpSafetyEnabled;
            printf("Pump safety %s\n",
                   pumpSafetyEnabled ? "ENABLED - pump may run" : "DISABLED - pump locked out");
        }
    }
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
    leds.clear();
    leds.show();
 
    // --- Sensor / peripheral init ---
    HTU21D htu21d(I2C_PORT);
    bool htuOk = htu21d.init();
    printf("HTU21D init: %s\n", htuOk ? "OK" : "FAILED");
 
    BH1750 bh1750(I2C_PORT);
    bool bh1750Ok = bh1750.init();
    printf("BH1750 init: %s\n", bh1750Ok ? "OK" : "FAILED");
 
    soil_moisture_init();
    soil_moisture_calibrate(SOIL_DRY_RAW_DEFAULT, SOIL_WET_RAW_DEFAULT);  // dry=3340, wet=1230

    pump_init();
 
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

    // --- Button Setup ---
    gpio_init(SWITCH_BUTTON_PIN);
    gpio_set_dir(SWITCH_BUTTON_PIN, GPIO_IN);
    gpio_pull_down(SWITCH_BUTTON_PIN);
 
    leds.show();
    printf("Sensor logging starting...\n");
 
    while (true) {
        // Soil moisture
        uint16_t raw = soil_moisture_read_raw();
        float moistVoltage = soil_moisture_raw_to_voltage(raw);
        float moisturePct = soil_moisture_raw_to_percentage(raw);
        printf("Raw: %u, Voltage: %.2f V, Moisture: %.2f%%\n", raw, moistVoltage, moisturePct);

        // Water the plant if soil moisture has dropped below the configured
        // minimum. Pump runs briefly, then the loop continues on to its next
        // normal read cycle to re-check the level rather than dosing in one
        // long continuous run.
        bool needsWater = moisturePct < SOIL_MOISTURE_MIN_PERCENT;
        if (needsWater && pumpSafetyEnabled) {
            printf("Soil moisture %.2f%% below minimum %.1f%% - running pump for %dms\n",
                   moisturePct, SOIL_MOISTURE_MIN_PERCENT, PUMP_RUN_MS);
            pump_on();
            sleep_ms(PUMP_RUN_MS);
            pump_off();
        } else if (needsWater && !pumpSafetyEnabled) {
            printf("Soil moisture low but pump is safety-disabled - skipping watering\n");
        }
 
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
            } else {
                printf("BH1750 read failed\n");
            }
        }

       // --- Determine if readings are outside their healthy range ---
        // An alert is only ever raised for a reading we actually trust this
        // cycle (i.e. the corresponding sensor read succeeded).
        bool humidityAlert = envOk && (humidityRH < RH_MIN_PERCENT || humidityRH > RH_MAX_PERCENT);
        bool tempAlert     = envOk && (tempC < TEMP_MIN_C || tempC > TEMP_MAX_C);
        bool lightAlert    = luxOk && (lux < AMBIENT_LIGHT_MIN_LUX || lux > AMBIENT_LIGHT_MAX_LUX);
        bool moistureAlert = moisturePct < SOIL_MOISTURE_MIN_PERCENT || moisturePct > SOIL_MOISTURE_MAX_PERCENT;
 
        if (humidityAlert) printf("ALERT: Humidity %.2f %%RH outside [%.1f, %.1f]\n", humidityRH, RH_MIN_PERCENT, RH_MAX_PERCENT);
        if (tempAlert)     printf("ALERT: Temp %.2f C outside [%.1f, %.1f]\n", tempC, TEMP_MIN_C, TEMP_MAX_C);
        if (lightAlert)    printf("ALERT: Light %.1f lx outside [%.1f, %.1f]\n", lux, AMBIENT_LIGHT_MIN_LUX, AMBIENT_LIGHT_MAX_LUX);
        if (moistureAlert) printf("ALERT: Moisture %.2f%% outside [%.1f, %.1f]\n", moisturePct, SOIL_MOISTURE_MIN_PERCENT, SOIL_MOISTURE_MAX_PERCENT); 
 
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

            // Poll the pump safety toggle button on this same 10ms tick.
            pollSafetyButton();

            // Drive the red alert flashes off this same 10ms tick so the
            // LEDs blink smoothly without blocking the BLE polling above.
            // Light has no LED of its own - it's reported as a lux/day
            // integral rather than a live reading, so it isn't a good fit
            // for an instantaneous flash alert.
            bool changed = false;
            changed |= updateAlertLED(leds, LED_HUMIDITY_INDEX, humidityAlert, elapsed_ms);
            changed |= updateAlertLED(leds, LED_TEMP_INDEX,     tempAlert,     elapsed_ms);
            changed |= updateAlertLED(leds, LED_MOISTURE_INDEX, moistureAlert, elapsed_ms);
            if (changed) {
                leds.show();
            }

            sleep_ms(10);
            elapsed_ms += 10;
        }
    }
 
    tf015_close();
    return 0;
}