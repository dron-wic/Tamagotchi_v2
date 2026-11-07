#pragma once

#include <stdint.h>

void daily_init();
void daily_tick(uint32_t now);

bool daily_auto_ntp();
void daily_set_auto_ntp(bool on);
bool daily_night_pet();
void daily_set_night_pet(bool on);
bool daily_quiet_night();
void daily_set_quiet_night(bool on);
// Wake toasts: «Пора покормить», «Пора помыть», … (not battery).
bool daily_care_notify();
void daily_set_care_notify(bool on);

// True during local 22:00–08:00 when clock is known.
bool daily_is_night();
// Mute click LED overnight when quiet_night is on.
bool daily_silence_led();
