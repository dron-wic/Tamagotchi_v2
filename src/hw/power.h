#pragma once

#include <stdint.h>

void power_init();
void power_tick();
void power_set_busy(bool busy);
bool power_swallow_touch();
// Edge: screen just woke from backlight-off. Cleared on read.
bool power_take_wake();
bool power_screen_off();
int power_battery_percent();
bool power_battery_valid();
bool power_charging();
bool power_plugged();
bool power_charge_done();
void power_backlight(uint8_t level);
// dim_ms or off_ms of 0 means that step never happens.
void power_configure(uint32_t dim_ms, uint32_t off_ms, uint8_t active);
