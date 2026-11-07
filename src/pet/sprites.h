#pragma once

#include <stdint.h>

struct RgbSprite {
    const uint16_t *px;
    uint16_t w;
    uint16_t h;
};

void sprites_init();
bool sprites_get(uint8_t species, RgbSprite *out);
