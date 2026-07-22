#include "BH1750FVI-TR.h"

#include "pico/time.h"

namespace {
// Max conversion time for One-Time H-Resolution Mode per datasheet
// (typical 120ms, worst case 180ms).
constexpr uint32_t kMeasurementDelayMs = 180;
}  // namespace

// Just stores the bus handle and address - no I2C traffic here, so
// construction can never fail or block.
BH1750::BH1750(i2c_inst_t* i2c, uint8_t address)
    : i2c_(i2c), address_(address) {}

// Single-byte opcode write, shared by init() and readLux(). The BH1750
// takes bare opcodes with no register/data bytes
bool BH1750::writeCommand(Command cmd) {
    uint8_t cmdByte = static_cast<uint8_t>(cmd);
    int written = i2c_write_blocking(i2c_, address_, &cmdByte, 1, false);
    return written == 1;
}

// Powers on, then resets the illuminance data register. Reset must be
// sent after Power On per datasheet (it's ignored in Power Down state),
// so the two are sequenced here rather than left to the caller. Also
// doubles as a presence check via the Power On ACK.
bool BH1750::init() {
    if (!writeCommand(Command::kPowerOn)) {
        return false;
    }
    return writeCommand(Command::kReset);
}

// Sends the One-Time H-Resolution opcode, blocks for the worst-case
// conversion time, then reads back the 2-byte result and
// converts it to lux. The sensor returns to Power Down automatically
// once the one-time measurement completes, so no cleanup call is needed.
bool BH1750::readLux(float& luxOut) {
    if (!writeCommand(Command::kOneTimeHRes)) {
        return false;
    }

    sleep_ms(kMeasurementDelayMs);

    uint8_t buf[2] = {0};
    int read = i2c_read_blocking(i2c_, address_, buf, 2, false);
    if (read != 2) {
        return false;
    }

    uint16_t raw = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    // Datasheet formula for default measurement time (MTreg = 69):
    // lux = raw / 1.2
    luxOut = static_cast<float>(raw) / 1.2f;
    return true;
}