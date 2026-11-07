#pragma once

#include <stdint.h>

struct TouchPoint {
    int16_t x;
    int16_t y;
    bool down;
    bool pressed;
};

void touch_init();
void touch_poll();
TouchPoint touch_point();
bool touch_take_press();
bool touch_take_home();
void touch_set_mode(uint8_t mode);
uint8_t touch_mode();
