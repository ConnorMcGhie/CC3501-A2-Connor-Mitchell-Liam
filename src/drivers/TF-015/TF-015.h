#pragma once

#include <cstdint>
#include <cstddef>
#include "board.h"

// Initialise 
bool tf015_init();

// Appends one line of text to this run's log file
bool tf015_log_line(const char *text);

// Closes the log file and unmounts the filesystem. Call before power off.
void tf015_close();
