#pragma once

#include <stdint.h>
#include "hardware/pio.h"
#include "board.h"


// A single RGB colour
struct Colour {
    uint8_t red, green, blue;
};

// A single LED index + colour pair, used for set_multiple()
struct LEDUpdate {
    uint8_t index;
    Colour colour;
};

// A colour in HSV format (hue 0-359, saturation 0-255, and value 0-255)
struct HSV {
    uint16_t hue;
    uint8_t  sat;
    uint8_t  val;
};

// Convert HSV to Colour (RGB)
Colour hsv_to_rgb(HSV hsv);

// Predefined colours for convenience
namespace Colours {
    constexpr Colour OFF     = {0,   0,   0};
    constexpr Colour RED     = {255, 0,   0};
    constexpr Colour GREEN   = {0,   255, 0};
    constexpr Colour BLUE    = {0,   0,   255};
    constexpr Colour WHITE   = {255, 255, 255};
    constexpr Colour ORANGE  = {255, 128, 0};
    constexpr Colour PURPLE  = {128, 0,   128};
}

class LEDDriver {
public:
    // Pass in the PIO instance and state machine set up by the WS2812 driver
    LEDDriver(PIO pio, uint sm);

    // Set a single LED colour (staged until show() is called)
    void set(uint8_t index, Colour colour);

    // Set a single LED by HSV colour
    void set_hsv(uint8_t index, HSV colour);

    // Set multiple LEDs in one call using an array of LEDUpdate
    void set_multiple(const LEDUpdate* updates, uint8_t count);

    // Send staged colours to the hardware
    void show();

    // Stage all LEDs off (call show() to send)
    void clear();

    // Get the staged colour of one LED
    Colour get(uint8_t index) const;

    // Returns true if set() has been called since the last show()
    bool is_dirty() const;

private:
    PIO    _pio;
    uint   _sm;
    Colour _state[NUM_LEDS];
    bool   _dirty = false;
};