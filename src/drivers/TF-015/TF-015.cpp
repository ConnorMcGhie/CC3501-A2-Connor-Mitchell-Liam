#include "TF-015.h"
#include "board.h"
#include <cstring>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"

// State 
static bool s_ready = false; // Indicates if the SD card is initialized and ready for operations
static uint8_t s_buf[TF015_BLOCK_SIZE]; // Buffer for accumulating log lines before writing to SD card
static size_t s_bufLen = 0; // Current length of data in the buffer
static uint32_t s_nextLogBlock = 0; 

// SPI helpers
static uint8_t xfer(uint8_t out) {
    uint8_t in;
    spi_write_read_blocking(SD_SPI_PORT, &out, &in, 1);
    return in;
}
 
static void select()   { gpio_put(SD_PIN_CS, 0); }
static void deselect() { gpio_put(SD_PIN_CS, 1); xfer(0xFF); }
 
static uint8_t send_cmd(uint8_t idx, uint32_t arg, uint8_t crc) {
    deselect();
    select();
    uint8_t frame[6] = {
        static_cast<uint8_t>(0x40 | idx),
        static_cast<uint8_t>(arg >> 24), static_cast<uint8_t>(arg >> 16),
        static_cast<uint8_t>(arg >> 8),  static_cast<uint8_t>(arg),
        crc
    };
    for (uint8_t b : frame) xfer(b);
 
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 8; i++) { r1 = xfer(0xFF); if (!(r1 & 0x80)) break; }
    return r1;
}
 
static bool wait_data_token() {
    absolute_time_t deadline = make_timeout_time_ms(200);
    uint8_t t;
    do { t = xfer(0xFF); } while (t == 0xFF && !time_reached(deadline));
    return t == 0xFE;
}

// Init
bool tf015_init() {
    s_bufLen = 0;
    s_nextLogBlock = SD_LOG_START_BLOCK;
    s_ready = false;
 
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);
 
    gpio_init(SD_PIN_CD);
    gpio_set_dir(SD_PIN_CD, GPIO_IN);
    gpio_pull_up(SD_PIN_CD);
 
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    spi_init(SD_SPI_PORT, SD_INIT_HZ);
    spi_set_format(SD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
 
    sleep_ms(2);
    for (int i = 0; i < 10; i++) xfer(0xFF);              // 80 dummy clocks
 
    if (send_cmd(0, 0, 0x95) != 0x01) return false;        // CMD0 = idle
    send_cmd(8, 0x1AA, 0x87);                                // CMD8
    for (int i = 0; i < 4; i++) xfer(0xFF);                 // clear R7 tail
 
    absolute_time_t deadline = make_timeout_time_ms(1000);
    uint8_t r1;
    do {                                                     // ACMD41 until ready
        send_cmd(55, 0, 0x01);
        r1 = send_cmd(41, 1UL << 30, 0x01);
    } while (r1 != 0x00 && !time_reached(deadline));
    if (r1 != 0x00) return false;
 
    deselect();
    spi_set_baudrate(SD_SPI_PORT, SD_OP_HZ);                // full speed
    s_ready = true;
    return true;
}
 
bool tf015_is_ready() { return s_ready; }
 
bool tf015_card_present() {
    return gpio_get(SD_PIN_CD) == 0;  // LOW = card inserted on most modules
}

// Raw Blocks 
bool tf015_read_block(uint32_t block, uint8_t *dst) {
    if (!s_ready || send_cmd(17, block, 0x01) != 0x00) { deselect(); return false; }
    if (!wait_data_token()) { deselect(); return false; }
 
    for (size_t i = 0; i < TF015_BLOCK_SIZE; i++) dst[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);                                 // discard CRC
    deselect();
    return true;
}
 
bool tf015_write_block(uint32_t block, const uint8_t *src) {
    if (!s_ready || send_cmd(24, block, 0x01) != 0x00) { deselect(); return false; }
 
    xfer(0xFE);
    for (size_t i = 0; i < TF015_BLOCK_SIZE; i++) xfer(src[i]);
    xfer(0xFF); xfer(0xFF);                                 // dummy CRC
    if ((xfer(0xFF) & 0x1F) != 0x05) { deselect(); return false; }  // rejected
 
    absolute_time_t deadline = make_timeout_time_ms(500);
    while (xfer(0xFF) == 0x00 && !time_reached(deadline)) {}  // wait busy
    deselect();
    return true;
}

// Logging
static bool write_buffer_as_block() {
    if (!tf015_write_block(s_nextLogBlock, s_buf)) return false;
    s_nextLogBlock++;
    s_bufLen = 0;
    return true;
}
 
bool tf015_log_line(const char *text) {
    size_t len = strlen(text);
 
    for (size_t i = 0; i < len; i++) {
        s_buf[s_bufLen++] = static_cast<uint8_t>(text[i]);
        if (s_bufLen == TF015_BLOCK_SIZE) {
            if (!write_buffer_as_block()) return false;
        }
    }
 
    s_buf[s_bufLen++] = '\n';
    if (s_bufLen == TF015_BLOCK_SIZE) {
        if (!write_buffer_as_block()) return false;
    }
 
    return true;
}
 
bool tf015_flush() {
    if (s_bufLen == 0) return true;
    while (s_bufLen < TF015_BLOCK_SIZE) s_buf[s_bufLen++] = ' ';
    return write_buffer_as_block();
}