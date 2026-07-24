#pragma once

#include <cstdint>
#include <cstddef>

#define TF015_BLOCK_SIZE 512

bool tf015_init(); // Brings up the SD card and resets the logger 

// Read/write one 512-byte block, buffer must have room
bool tf015_read_block(uint32_t block, uint8_t *dst);
bool tf015_write_block(uint32_t block, const uint8_t *src);

// Appends one line of text to the log
// Once enough lines accumulate to fill a 512-byte block, its written to the SD card
bool tf015_log_line(const char *text);

// Writes out whatever is currently buffered, even if it doesn't fill a full block
bool tf015_flush();

bool tf015_is_ready();

// Reads the SD_CD (card detect) pin
bool tf015_card_present();

