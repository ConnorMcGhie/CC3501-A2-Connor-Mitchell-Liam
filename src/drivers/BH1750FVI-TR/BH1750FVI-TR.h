#pragma once

#include <cstdint>

#include "hardware/i2c.h"

// Driver for the ROHM BH1750FVI-TR digital ambient light sensor.
//
// Same scope/ownership rules as the HTU21D driver: this class only sends
// commands, reads bytes, and converts raw counts to lux. It doesn't decide
// what a given lux value means for a plant - that's a higher-level concern.
// The I2C bus is shared and expected to already be initialised elsewhere
// (see board.h) - this class only ever takes a handle to it.
class BH1750 {
public:
    // Default address when ADDR is tied low, as per the schematic. Pass
    // kAltAddress if a second BH1750 is ever added with ADDR pulled high.
    static constexpr uint8_t kDefaultAddress = 0x23;
    static constexpr uint8_t kAltAddress     = 0x5C;

    explicit BH1750(i2c_inst_t* i2c, uint8_t address = kDefaultAddress);

    // Powers the sensor on and resets its illuminance register. Returns
    // false if the device does not ACK.
    bool init();

    // Triggers a One-Time H-Resolution measurement (1 lx resolution),
    // blocks for the datasheet's max conversion time (180ms), then reads
    // and converts the 16-bit result into lux. The sensor auto-powers-down
    // after each one-time measurement, so no explicit power-down call is
    // needed between reads.
    // Returns false on I2C failure; output is untouched on failure.
    bool readLux(float& luxOut);

private:
    // Opcodes from the BH1750FVI datasheet's instruction set. Only the
    // subset this driver currently uses is listed.
    enum class Command : uint8_t {
        kPowerDown        = 0x00,
        kPowerOn          = 0x01,
        kReset            = 0x07,  // only valid after Power On
        kOneTimeHRes      = 0x20,  // 1 lx resolution, typ. 120ms, max 180ms
    };

    bool writeCommand(Command cmd);

    i2c_inst_t* i2c_;
    uint8_t address_;
};