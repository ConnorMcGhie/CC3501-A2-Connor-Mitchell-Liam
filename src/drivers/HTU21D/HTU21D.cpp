#include "HTU21D.h"

#include "pico/time.h"

namespace {
// CRC-8 polynomial x^8 + x^5 + x^4 + 1, as specified in the HTU21D
// datasheet checksum section (top bit implicit, so 0x31 here).
constexpr uint8_t kCrcPolynomial = 0x31;

// Soft reset re-loads calibration data from OTP memory; datasheet gives
// max 15ms for this to complete.
constexpr uint32_t kSoftResetDelayMs = 15;
}  // namespace

// Just stores the bus handle and address - no I2C traffic here, so
// construction can never fail or block.
HTU21D::HTU21D(i2c_inst_t* i2c, uint8_t address)
    : i2c_(i2c), address_(address) {}

// Sends soft-reset and waits out the settle time. Also doubles as a
// presence check: if the sensor doesn't ACK here, it's a wiring/power
// issue rather than a measurement issue.
bool HTU21D::init() {
    uint8_t cmd = static_cast<uint8_t>(Command::kSoftReset);
    int written = i2c_write_blocking(i2c_, address_, &cmd, 1, false);
    if (written != 1) {
        return false;
    }
    sleep_ms(kSoftResetDelayMs);
    return true;
}

// Shared path for both measurement types - only the command byte
// differs. Sends the trigger command, then reads 3 bytes back
// (MSB, LSB, CRC); the read blocks because the sensor stretches SCL
// until conversion is done (handled transparently by the RP2040's
// I2C hardware, no manual polling needed). Validates the CRC, then
// masks off the 2 status bits that aren't part of the measurement.
bool HTU21D::triggerMeasurement(Command cmd, uint16_t& rawOut) {
    uint8_t cmdByte = static_cast<uint8_t>(cmd);
    int written = i2c_write_blocking(i2c_, address_, &cmdByte, 1, true);
    if (written != 1) {
        return false;
    }

    uint8_t buf[3] = {0};
    int read = i2c_read_blocking(i2c_, address_, buf, 3, false);
    if (read != 3) {
        return false;
    }

    if (crc8(buf, 2) != buf[2]) {
        return false;
    }

    rawOut = (static_cast<uint16_t>(buf[0]) << 8 | buf[1]) & 0xFFFC;
    return true;
}

// Thin wrappers applying the datasheet's linear conversion formulas to
// a validated raw count.
bool HTU21D::readTemperature(float& temperatureC) {
    uint16_t raw = 0;
    if (!triggerMeasurement(Command::kTriggerTempHold, raw)) {
        return false;
    }
    // Temp (degC) = -46.85 + 175.72 * (S_temp / 2^16)
    temperatureC = -46.85f + 175.72f * (static_cast<float>(raw) / 65536.0f);
    return true;
}

bool HTU21D::readHumidity(float& humidityRH) {
    uint16_t raw = 0;
    if (!triggerMeasurement(Command::kTriggerHumidityHold, raw)) {
        return false;
    }
    // RH (%) = -6 + 125 * (S_RH / 2^16)
    humidityRH = -6.0f + 125.0f * (static_cast<float>(raw) / 65536.0f);
    return true;
}

// Standard bitwise CRC-8: XOR each byte in, then shift 8 times,
// applying the polynomial whenever the top bit is set.
uint8_t HTU21D::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ kCrcPolynomial)
                                : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}