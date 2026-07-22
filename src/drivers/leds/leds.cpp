#include "leds.h"

// HSV to RGB

Colour hsv_to_rgb(HSV hsv) {
    if (hsv.sat == 0) {
        return {hsv.val, hsv.val, hsv.val};
    }

    uint8_t sector   = hsv.hue / 60;
    uint8_t fraction = (hsv.hue % 60) * 255 / 60;

    uint8_t v = hsv.val;
    uint8_t p = (uint32_t)v * (255 - hsv.sat) / 255;
    uint8_t q = (uint32_t)v * (255 - (uint32_t)hsv.sat * fraction / 255) / 255;
    uint8_t t = (uint32_t)v * (255 - (uint32_t)hsv.sat * (255 - fraction) / 255) / 255;

    switch (sector) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

// LEDDriver

LEDDriver::LEDDriver(PIO pio, uint sm) : _pio(pio), _sm(sm)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        _state[i] = Colours::OFF;
    }
    show();
}

void LEDDriver::set(uint8_t index, Colour colour)
{
    if (index >= NUM_LEDS) return;
    _state[index] = colour;
    _dirty = true;
}

void LEDDriver::set_hsv(uint8_t index, HSV colour)
{
    set(index, hsv_to_rgb(colour));
}

void LEDDriver::set_multiple(const LEDUpdate* updates, uint8_t count)
{
    for (int i = 0; i < count; i++) {
        set(updates[i].index, updates[i].colour);
    }
}

void LEDDriver::show()
{
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t word = ((uint32_t)_state[i].red   << 24)
                      | ((uint32_t)_state[i].green << 16)
                      | ((uint32_t)_state[i].blue  <<  8);
        pio_sm_put_blocking(_pio, _sm, word);
    }
    _dirty = false;
}

void LEDDriver::clear()
{
    for (int i = 0; i < NUM_LEDS; i++) {
        _state[i] = Colours::OFF;
    }
    _dirty = true;
}

Colour LEDDriver::get(uint8_t index) const
{
    if (index >= NUM_LEDS) return Colours::OFF;
    return _state[index];
}

bool LEDDriver::is_dirty() const
{
    return _dirty;
}