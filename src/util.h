#pragma once

#include <stddef.h>
#include <stdint.h>

void *ps_alloc(size_t bytes);
void ps_free(void *p);

template <typename T>
T clampv(T v, T lo, T hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t rgb565_hex(uint32_t hex)
{
    return rgb565((uint8_t)((hex >> 16) & 255), (uint8_t)((hex >> 8) & 255), (uint8_t)(hex & 255));
}
