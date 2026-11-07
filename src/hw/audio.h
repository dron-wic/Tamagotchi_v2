#pragma once

#include <stdint.h>

void audio_init();
void audio_tick(uint32_t now);
void audio_click();
void audio_ok();
void audio_fail();
void audio_coin();
void audio_set_led(bool on);
