#include "hw/input.h"

#include "board_config.h"
#include "storage/save.h"

#include <Arduino.h>

struct Key {
    uint8_t pin;
    bool down;
    bool edge;
    uint32_t last;
};

static Key keys[3] = {
    {PIN_BTN_BACK, false, false, 0},
    {PIN_BTN_OK, false, false, 0},
    {PIN_BTN_NEXT, false, false, 0},
};
static uint32_t last_activity = 0;

void input_mark()
{
    last_activity = millis();
}

uint32_t input_last_ms()
{
    return last_activity;
}

void input_init()
{
    for (Key &k : keys) {
        pinMode(k.pin, INPUT_PULLUP);
    }
    last_activity = millis();
}

void input_poll()
{
    uint32_t now = millis();
    for (Key &k : keys) {
        bool down = digitalRead(k.pin) == LOW;
        if (down != k.down && (now - k.last) > 30) {
            k.down = down;
            k.last = now;
            if (down) {
                k.edge = true;
                last_activity = now;
            }
        }
    }
}

bool input_take(ButtonId id)
{
    Key &k = keys[id];
    bool was = k.edge;
    k.edge = false;
    return was;
}

bool input_take_action()
{
    bool hit = false;
    for (uint8_t i = 0; i < 3; i++) {
        if (save_btn_act(i) != BTN_ACT_ACTION) continue;
        if (input_take((ButtonId)i)) hit = true;
    }
    return hit;
}
