#pragma once

#include <stdint.h>

enum ButtonId : uint8_t {
    BTN_BACK = 0,
    BTN_OK = 1,
    BTN_NEXT = 2,
};

void input_init();
void input_poll();
bool input_take(ButtonId id);
// Edge of every key whose saved action is "Действие".
bool input_take_action();
uint32_t input_last_ms();
void input_mark();
