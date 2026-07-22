#pragma once

#include <cstddef>
#include <cstdint>

#include "hardware/i2c.h"

// Driver for the TE Connectivity HTU21D digital humidity/temperature sensor.
//
// This driver owns only device-level protocol logic. The I2C peripheral
// (i2c0) is expected to be initialised once at board-init time and shared
// with other devices on the same bus (e.g. BH1750); this class just takes
// a handle to that already-configured bus.
class HTU21D {
public:
    static constexpr uint8_t kDefaultAddress = 0x40;

    explicit HTU21D(i2c_inst_t* i2c, uint8_t address = kDefaultAddress);

    // Soft-resets the sensor. Returns false if the device does not ACK.
    bool init();

    // Triggers a measurement (hold-master mode) and converts the raw
    // reading into engineering units. Each call blocks for the duration
    // of the conversion (~50ms max for temperature, ~16ms max for
    // humidity) because the sensor stretches SCL until data is ready.
    // Returns false on I2C failure or CRC mismatch; output is untouched
    // on failure.
    bool readTemperature(float& temperatureC);
    bool readHumidity(float& humidityRH);

private:
    // Command bytes from the HTU21D datasheet's command table.
    enum class Command : uint8_t {
        kTriggerTempHold     = 0xE3,
        kTriggerHumidityHold = 0xE5,
        kWriteUserRegister   = 0xE6,
        kReadUserRegister    = 0xE7,
        kSoftReset           = 0xFE,
    };

    // Sends a trigger-measurement command, blocks for the sensor's clock
    // stretch, reads back 2 data bytes + 1 CRC byte, verifies the CRC,
    // and masks off the 2 status bits in the raw result.
    bool triggerMeasurement(Command cmd, uint16_t& rawOut);

    // CRC-8 check per the datasheet (polynomial x^8+x^5+x^4+1), used to
    // validate the 2 data bytes against the sensor's checksum byte.
    static uint8_t crc8(const uint8_t* data, size_t len);

    i2c_inst_t* i2c_;
    uint8_t address_;
};