#include "TF-015.h"
#include "board.h"
#include <cstring>
#include <cstdio>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "ff.h"
#include "hw_config.h"


// Tells the FatFs_SPI library which pins to use
// Functions that hw_config.h expects to be defined
static spi_t s_spi = {};
static sd_card_t s_sd_card = {};

static void init_hw_config() {
    s_spi.hw_inst = spi0;
    s_spi.miso_gpio = SD_PIN_MISO;
    s_spi.mosi_gpio = SD_PIN_MOSI;
    s_spi.sck_gpio = SD_PIN_SCK;
    s_spi.baud_rate = SD_OP_HZ;
 
    s_sd_card.pcName = "0:";
    s_sd_card.spi = &s_spi;
    s_sd_card.ss_gpio = SD_PIN_CS;
    s_sd_card.use_card_detect = true;
    s_sd_card.card_detect_gpio = SD_PIN_CD;
    s_sd_card.card_detected_true = 0;  
}

size_t sd_get_num() { return 1; }
sd_card_t *sd_get_by_num(size_t num) { return (num == 0) ? &s_sd_card : nullptr; }
size_t spi_get_num() { return 1; }
spi_t *spi_get_by_num(size_t num) { return (num == 0) ? &s_spi : nullptr; }
 
static FATFS s_fs;
static FIL s_file;
static bool s_open = false;
 
bool tf015_init() {
    s_open = false;
    init_hw_config();
 
    if (f_mount(&s_fs, "0:", 1) != FR_OK) {
        return false;
    }

    // Find the first log number that doesn't exist yet: LOG001.CSV,
    // LOG002.CSV, FA_CREATE_NEW fails with FR_EXIST if already taken.
    char path[32];
    FRESULT res;
    for (int n = 1; n <= 999; n++) {
        snprintf(path, sizeof(path), "%s%03d.CSV", SD_LOG_BASENAME, n);
        res = f_open(&s_file, path, FA_CREATE_NEW | FA_WRITE);
        if (res == FR_OK) {
            s_open = true;
            return true;
        }
        if (res != FR_EXIST) {
            return false;  // real error, not name taken
        }
    }
    return false;  // ran out of numbers
}

bool tf015_log_line(const char *text) {
    if (!s_open) return false;
 
    UINT written;
    size_t len = strlen(text);
 
    if (f_write(&s_file, text, len, &written) != FR_OK || written != len) return false;
    if (f_write(&s_file, "\n", 1, &written) != FR_OK || written != 1) return false;
 
    return f_sync(&s_file) == FR_OK;  // flush now so this line survives a power loss
}
 
void tf015_close() {
    if (s_open) {
        f_close(&s_file);
        s_open = false;
    }
    f_unmount("0:");
}