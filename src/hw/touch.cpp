#include "hw/touch.h"

#include "board_config.h"
#include "hw/input.h"
#include "hw/power.h"
#include "util.h"

#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>

static TouchDrvCSTXXX drv;
static bool ready = false;
static TouchPoint cur = {0, 0, false, false};
static uint8_t mode = 0;
static volatile bool home_edge = false;
static uint32_t home_lock_until = 0;
static uint32_t next_probe = 0;
static uint32_t seen_down_ms = 0;
static uint8_t fail_streak = 0;

static void map_point(int16_t x, int16_t y, int16_t *ox, int16_t *oy)
{
    int16_t px = 0;
    int16_t py = 0;
    switch (mode & 7) {
    case 0: px = (int16_t)(LCD_W - 1 - y); py = x; break;
    case 1: px = y; py = x; break;
    case 2: px = y; py = (int16_t)(LCD_H - 1 - x); break;
    case 3: px = (int16_t)(LCD_W - 1 - y); py = (int16_t)(LCD_H - 1 - x); break;
    case 4: px = x; py = y; break;
    case 5: px = (int16_t)(LCD_W - 1 - x); py = y; break;
    case 6: px = x; py = (int16_t)(LCD_H - 1 - y); break;
    default: px = (int16_t)(LCD_W - 1 - x); py = (int16_t)(LCD_H - 1 - y); break;
    }
    *ox = (int16_t)clampv<int>(px, 0, LCD_W - 1);
    *oy = (int16_t)clampv<int>(py, 0, LCD_H - 1);
}

static void on_home_key(void *user_data)
{
    (void)user_data;
    uint32_t now = millis();
    if ((int32_t)(now - home_lock_until) < 0) return;
    home_lock_until = now + 200;
    home_edge = true;
    input_mark();
}

static bool touch_begin()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(50);
    drv.setPins(PIN_TOUCH_RST, PIN_TOUCH_IRQ);
    ready = drv.begin(Wire, CST226SE_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!ready) return false;
    drv.setMaxCoordinates(LCD_H, LCD_W);
    drv.setSwapXY(true);
    drv.setMirrorXY(false, true);
    drv.setHomeButtonCallback(on_home_key, nullptr);
    return true;
}

void touch_init()
{
    if (!touch_begin()) {
        Serial.println("Touch not found");
        return;
    }
    Serial.printf("Touch %s\n", drv.getModelName());
}

void touch_set_mode(uint8_t m)
{
    mode = m & 7;
}

uint8_t touch_mode()
{
    return mode;
}

static bool touch_acks()
{
    Wire.beginTransmission(CST226SE_SLAVE_ADDRESS);
    return Wire.endTransmission() == 0;
}

void touch_poll()
{
    cur.pressed = false;
    // Battery + dark screen: leave the touch chip alone. A physical key wakes.
    if (power_screen_off() && !power_plugged() && !power_charging() && !power_charge_done()) {
        cur.down = false;
        return;
    }
    uint32_t now = millis();
    if ((int32_t)(now - next_probe) >= 0) {
        next_probe = now + 10000;
        if (!ready || !touch_acks()) {
            if (++fail_streak >= 3) {
                fail_streak = 0;
                Serial.println("Touch recover");
                if (ready) drv.reset();
                touch_begin();
            }
        } else {
            fail_streak = 0;
        }
    }
    if (!ready) {
        cur.down = false;
        return;
    }
    int16_t x = 0;
    int16_t y = 0;
    uint8_t n = drv.getPoint(&x, &y, 1);
    bool down = n > 0;
    if (down) {
        seen_down_ms = now;
        if (!cur.down) cur.pressed = true;
        cur.down = true;
        int16_t mx, my;
        map_point(x, y, &mx, &my);
        cur.x = mx;
        cur.y = my;
    } else if (cur.down && (uint32_t)(now - seen_down_ms) < 50) {
        // The controller reports "up" between frames. That release was a click.
    } else {
        cur.down = false;
    }
}

TouchPoint touch_point()
{
    return cur;
}

bool touch_take_press()
{
    bool was = cur.pressed;
    cur.pressed = false;
    return was;
}

bool touch_take_home()
{
    bool was = home_edge;
    home_edge = false;
    return was;
}
