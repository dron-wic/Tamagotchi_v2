#pragma once

#include <stdint.h>

uint8_t pref_dim_count();
const char *pref_dim_name(uint8_t idx);
uint32_t pref_dim_ms(uint8_t idx);

uint8_t pref_off_count();
const char *pref_off_name(uint8_t idx);
uint32_t pref_off_ms(uint8_t idx);

uint8_t pref_bl_count();
const char *pref_bl_name(uint8_t idx);
uint8_t pref_bl_level(uint8_t idx);

uint8_t pref_trans_count();
const char *pref_trans_name(uint8_t idx);

// Push the saved theme, backlight and LED flash into the hardware.
void prefs_apply();
