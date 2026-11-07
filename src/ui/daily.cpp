#include "ui/daily.h"

#include "hw/net.h"
#include "hw/power.h"
#include "hw/radio.h"
#include "pet/pet.h"
#include "ui/ui.h"

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

static const int NIGHT_START_H = 22;
static const int NIGHT_END_H = 8;
static const uint32_t NTP_RETRY_MS = 12u * 3600u * 1000u;
static const uint32_t BOOT_NTP_DELAY_MS = 8000;
static const uint32_t BATT_WARN_COOLDOWN_MS = 30u * 60u * 1000u;

static Preferences prefs;
static bool auto_ntp = true;
static bool night_pet = true;
static bool quiet_night = true;
static bool care_notify = true;

static bool auto_slept = false;
static bool boot_ntp_done = false;
static uint32_t next_ntp_at = 0;
static uint32_t last_batt_warn = 0;
static int last_night = -1; // -1 unknown, 0 day, 1 night

static void load()
{
    prefs.begin("daily", true);
    auto_ntp = prefs.getBool("auto_ntp", true);
    night_pet = prefs.getBool("night_pet", true);
    quiet_night = prefs.getBool("quiet", true);
    care_notify = prefs.getBool("care_ntf", true);
    prefs.end();
}

static void store()
{
    prefs.begin("daily", false);
    prefs.putBool("auto_ntp", auto_ntp);
    prefs.putBool("night_pet", night_pet);
    prefs.putBool("quiet", quiet_night);
    prefs.putBool("care_ntf", care_notify);
    prefs.end();
}

static int local_hour()
{
    time_t t = time(nullptr);
    if (t < 1700000000) return -1;
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_hour;
}

bool daily_is_night()
{
    int h = local_hour();
    if (h < 0) return false;
    return h >= NIGHT_START_H || h < NIGHT_END_H;
}

bool daily_silence_led()
{
    return quiet_night && daily_is_night();
}

bool daily_auto_ntp() { return auto_ntp; }
bool daily_night_pet() { return night_pet; }
bool daily_quiet_night() { return quiet_night; }
bool daily_care_notify() { return care_notify; }

void daily_set_auto_ntp(bool on)
{
    auto_ntp = on;
    store();
    if (on) next_ntp_at = millis() + 1000;
}

void daily_set_night_pet(bool on)
{
    night_pet = on;
    store();
    if (!on) auto_slept = false;
}

void daily_set_quiet_night(bool on)
{
    quiet_night = on;
    store();
}

void daily_set_care_notify(bool on)
{
    care_notify = on;
    store();
}

static void try_auto_ntp(uint32_t now)
{
    if (!auto_ntp) return;
    if (net_busy() || radio_busy()) return;
    if (!net_has_ssid()) return;
    // Never spin up Wi-Fi while the panel is asleep — that is the power-save window.
    if (power_screen_off()) return;
    if ((int32_t)(now - next_ntp_at) < 0) return;

    if (!net_start_sync()) {
        next_ntp_at = now + 60000;
        return;
    }
    Serial.println("Daily auto NTP");
    // Have clock → next refresh in 12 h; still missing → check again soon.
    next_ntp_at = now + (net_time_ok() ? NTP_RETRY_MS : 45000);
}

static void apply_night_pet()
{
    if (!night_pet || !pet_is_started() || !net_time_ok()) return;
    bool night = daily_is_night();
    int flag = night ? 1 : 0;
    if (flag == last_night) return;
    last_night = flag;

    if (night) {
        if (!pet_is_sleeping()) {
            pet_set_sleeping(true);
            auto_slept = true;
            ui_toast("Спокойной ночи");
        }
    } else if (auto_slept && pet_is_sleeping()) {
        pet_set_sleeping(false);
        auto_slept = false;
        ui_toast("Доброе утро");
    }
}

static void on_wake_care()
{
    if (!pet_is_started()) return;

    if (power_battery_valid() && power_battery_percent() <= 15 && !power_plugged()) {
        uint32_t now = millis();
        if ((uint32_t)(now - last_batt_warn) > BATT_WARN_COOLDOWN_MS) {
            last_batt_warn = now;
            ui_toast("Батарея садится");
            return; // one toast per wake cycle is enough
        }
    }

    // Character care toasts — optional (settings).
    if (!care_notify) return;

    uint8_t id = pet_active();
    if (pet_stat(id, STAT_HUNGER) < 25) {
        ui_toast("Пора покормить");
        return;
    }
    if (pet_stat(id, STAT_CLEAN) < 25) {
        ui_toast("Пора помыть");
        return;
    }
    if (pet_stat(id, STAT_HAPPY) < 25) {
        ui_toast("Питомцу грустно");
        return;
    }
    if (pet_stat(id, STAT_ENERGY) < 20 && !pet_is_sleeping()) {
        ui_toast("Нужен сон");
    }
}

void daily_init()
{
    load();
    next_ntp_at = millis() + BOOT_NTP_DELAY_MS;
    boot_ntp_done = false;
    last_night = -1;
    auto_slept = false;
}

void daily_tick(uint32_t now)
{
    if (!boot_ntp_done && (uint32_t)(now - 0) >= BOOT_NTP_DELAY_MS) {
        boot_ntp_done = true;
        try_auto_ntp(now);
    } else {
        try_auto_ntp(now);
    }

    // After NTP succeeds, night logic starts working.
    apply_night_pet();

    if (power_take_wake()) on_wake_care();
}
