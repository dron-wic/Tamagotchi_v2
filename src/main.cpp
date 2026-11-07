#include <Arduino.h>
#include <USB.h>
#include <WiFi.h>
#include <stdio.h>

#include "board_config.h"
#include "hw/audio.h"
#include "hw/display.h"
#include "hw/input.h"
#include "hw/net.h"
#include "hw/power.h"
#include "hw/radio.h"
#include "hw/sdcard.h"
#include "hw/touch.h"
#include "hw/usb_msc.h"
#include "pet/pet.h"
#include "pet/sprites.h"
#include "storage/save.h"
#include "ui/daily.h"
#include "ui/ui.h"
#include "version.h"

extern "C" bool btStop(void) __attribute__((weak));

void setup()
{
    WiFi.mode(WIFI_OFF);
    if (btStop) btStop();

    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    input_init();
    power_init();
    display_init();
    sd_init();
    // Disk only after the settings button. Otherwise leave USB-Serial/JTAG
    // on the cable so the PC sees a normal COM port.
    usb_msc_init();
    Serial.begin(115200);
    if (usb_msc_active()) {
        USB.begin();
        delay(150);
    }
    Serial.printf("\nTamagotchi  S3 Pro  %s\n", FW_VERSION_STR);
    if (usb_msc_active()) Serial.println("USB disk on");
    touch_init();
    audio_init();
    save_init();
    net_init();
    radio_init();
    daily_init();
    touch_set_mode(0);
    pet_init();
    sprites_init();
    ui_init();

    Serial.printf("heap %u  psram %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void loop()
{
    static uint32_t last = millis();
    uint32_t now = millis();
    uint32_t dt = now - last;
    last = now;

    input_poll();
    touch_poll();
    power_tick();
    usb_msc_tick();
    if (usb_msc_take_eject()) {
        ui_toast("Карта у устройства");
        delay(400);
        ESP.restart();
    }

    const bool asleep = power_screen_off();

    if (asleep) {
        // Power-save while the panel is off: no LVGL/SPI, low CPU, long sleep.
        // Keep pet time progressing with real dt (capped).
        if (dt > 500) dt = 500;
        pet_tick(dt);
        if (net_busy()) net_tick(now);
        if (radio_busy()) radio_tick(now);
        daily_tick(now); // night schedule; auto-NTP skipped while screen off
        save_tick(now);
        // Buttons are already polled. On battery the touch chip is left alone.
        delay(200);
        return;
    }

    if (dt > 80) dt = 80;
    pet_tick(dt);
    net_tick(now);
    radio_tick(now);
    daily_tick(now);
    {
        uint8_t gift = 0;
        if (radio_take_gift(&gift)) {
            pet_add_coins(gift);
            pet_reward(0, 6, 0, 1);
            char msg[24];
            snprintf(msg, sizeof(msg), "+%u монет", (unsigned)gift);
            ui_toast(msg);
        }
    }

    display_task();
    ui_tick(dt);
    save_tick(now);
    audio_tick(now);
    // Second pass flushes widget updates from this tick (canvas, labels).
    display_task();
    delay(1);
}
