#include "hw/power.h"

#include "board_config.h"
#include "hw/input.h"
#include "hw/touch.h"
#include "util.h"

#include <Arduino.h>
#include <Wire.h>

static uint8_t bl_level = 0;
static uint32_t dim_after = IDLE_DIM_MS;
static uint32_t off_after = IDLE_OFF_MS;
static uint8_t bl_active = BACKLIGHT_ACTIVE;
static uint8_t bl_dimmed = BACKLIGHT_DIM;
static bool busy = false;
static bool screen_off = false;
static bool swallow = false;
static bool wake_edge = false;
static int cpu_mhz = 240;
static bool pmu_ok = false;

static int batt_pct = -1;
static int filt_mv = 0;
static int filt_pct = -1;
static bool charging = false;
static bool plugged = false;
static bool charge_done = false;
static uint32_t batt_next = 0;

static bool i2c_read(uint8_t addr, uint8_t reg, uint8_t *out)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
    *out = Wire.read();
    return true;
}

static bool i2c_write(uint8_t addr, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void power_backlight(uint8_t level)
{
#if defined(USE_BL_PWM)
    level = level > 16 ? 16 : level;
    ledcWrite(1, level * 16);
    bl_level = level;
#else
    const uint8_t steps = 16;
    if (level > steps) level = steps;
    if (level == 0) {
        digitalWrite(PIN_TFT_BL, LOW);
        delay(3);
        bl_level = 0;
        return;
    }
    if (bl_level == 0) {
        digitalWrite(PIN_TFT_BL, HIGH);
        bl_level = steps;
        delayMicroseconds(30);
    }
    int from = steps - bl_level;
    int to = steps - level;
    int num = (steps + to - from) % steps;
    for (int i = 0; i < num; i++) {
        digitalWrite(PIN_TFT_BL, LOW);
        digitalWrite(PIN_TFT_BL, HIGH);
    }
    bl_level = level;
#endif
}

void power_configure(uint32_t dim_ms, uint32_t off_ms, uint8_t active)
{
    dim_after = dim_ms ? dim_ms : 0xFFFFFFFFu;
    off_after = off_ms ? off_ms : 0xFFFFFFFFu;
    if (active < 4) active = 4;
    if (active > 16) active = 16;
    bl_active = active;
    bl_dimmed = active > 8 ? 4 : 2;
    if (screen_off) return;
    uint32_t idle = millis() - input_last_ms();
    if (idle > off_after) return;
    power_backlight(idle > dim_after ? bl_dimmed : bl_active);
}

static int mv_to_pct(int mv)
{
    static const int16_t tab[][2] = {
        {3300, 0},  {3400, 3},  {3500, 8},  {3600, 18}, {3650, 28},
        {3700, 38}, {3750, 48}, {3800, 58}, {3850, 68}, {3900, 76},
        {3950, 83}, {4000, 89}, {4050, 94}, {4100, 97}, {4150, 99},
        {4200, 100},
    };
    const int n = (int)(sizeof(tab) / sizeof(tab[0]));
    if (mv <= tab[0][0]) return tab[0][1];
    if (mv >= tab[n - 1][0]) return tab[n - 1][1];
    for (int i = 1; i < n; i++) {
        if (mv <= tab[i][0]) {
            int mv0 = tab[i - 1][0];
            int mv1 = tab[i][0];
            int p0 = tab[i - 1][1];
            int p1 = tab[i][1];
            return p0 + (p1 - p0) * (mv - mv0) / (mv1 - mv0);
        }
    }
    return 0;
}

static int bat_reg_mv(uint8_t bat)
{
    int mv = 2304 + (int)(bat & 0x7F) * 20;
    if (mv < 2800 || mv > 4500) return 0;
    return mv;
}

// One conversion at boot. The periodic tick only reads the last result:
// CONV_START (REG02 bit 7) clears itself, and kicking it again every poll
// plus the old 25 ms wait blanked the ST7796 once per sample.
static bool read_bat_mv(int *out_mv)
{
    uint8_t bat = 0;
    if (!i2c_read(0x6A, 0x0E, &bat)) return false;
    int mv = bat_reg_mv(bat);
    if (!mv) return false;
    *out_mv = mv;
    return true;
}

static void read_charge_flags()
{
    uint8_t stat = 0;
    if (!i2c_read(0x6A, 0x0B, &stat)) return;
    uint8_t vbus = (stat >> 5) & 0x7;
    uint8_t chg = (stat >> 3) & 0x3;
    plugged = vbus != 0;
    charging = chg == 1 || chg == 2;
    charge_done = chg == 3;
}

static void apply_soc(int raw_pct)
{
    if (filt_pct < 0) {
        filt_pct = raw_pct;
        batt_pct = raw_pct;
        return;
    }

    filt_pct = (filt_pct * 7 + raw_pct) / 8;

    int shown = batt_pct;
    int target = filt_pct;
    if (charge_done || (charging && target >= 99)) target = 100;

    if (charging || charge_done) {
        if (target >= shown + 2) shown = target;
        if (charge_done) shown = 100;
    } else {
        if (target <= shown - 2) shown = target;
        if (target <= shown - 5) shown = shown - 1;
    }
    if (shown < 0) shown = 0;
    if (shown > 100) shown = 100;
    batt_pct = shown;
}

static void read_battery()
{
    if (!pmu_ok) return;

    int mv = 0;
    if (!read_bat_mv(&mv)) {
        uint8_t probe = 0;
        if (!i2c_read(0x6A, 0x0B, &probe)) pmu_ok = false;
        return;
    }

    if (filt_mv <= 0) filt_mv = mv;
    else filt_mv = (filt_mv * 3 + mv) / 4;

    read_charge_flags();

    int use_mv = filt_mv;
    if (charging) {
        use_mv -= 40;
        if (use_mv < 3300) use_mv = 3300;
    }
    apply_soc(mv_to_pct(use_mv));
}

static bool on_external_power()
{
    return plugged || charging || charge_done;
}

static void set_cpu_mhz(int mhz)
{
#if POWER_SCALE_CPU
    if (mhz == cpu_mhz) return;
    setCpuFrequencyMhz(mhz);
    cpu_mhz = mhz;
#else
    (void)mhz;
#endif
}

void power_init()
{
    pinMode(PIN_TFT_BL, OUTPUT);
#if defined(USE_BL_PWM)
    ledcSetup(1, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 1);
#endif
    power_backlight(BACKLIGHT_ACTIVE);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(50);
    Wire.beginTransmission(0x6A);
    pmu_ok = Wire.endTransmission() == 0;
    if (pmu_ok) {
        Serial.println("SY6970 online");
        uint8_t reg02 = 0;
        if (i2c_read(0x6A, 0x02, &reg02)) {
            i2c_write(0x6A, 0x02, (uint8_t)(reg02 | 0xC0));
        }
        delay(30);
        read_battery();
    } else {
        Serial.println("SY6970 not found");
    }
    batt_next = millis() + 2000;
}

void power_set_busy(bool on)
{
    busy = on;
}

bool power_swallow_touch()
{
    bool was = swallow;
    swallow = false;
    return was;
}

bool power_take_wake()
{
    bool was = wake_edge;
    wake_edge = false;
    return was;
}

bool power_screen_off()
{
    return screen_off;
}

int power_battery_percent()
{
    return batt_pct;
}

bool power_battery_valid()
{
    return batt_pct >= 0;
}

bool power_charging()
{
    return charging;
}

bool power_plugged()
{
    return plugged;
}

bool power_charge_done()
{
    return charge_done;
}

void power_tick()
{
    uint32_t now = millis();

    // No I2C while the CPU is at 80 MHz. That clock leaves the touch bus dead.
    bool battery_sleep = screen_off && !on_external_power();
    uint32_t period = screen_off ? 30000u : (on_external_power() ? 3000u : 8000u);
    if (!battery_sleep && (int32_t)(now - batt_next) >= 0) {
        // A finger is on the glass: the sample waits. Running it mid-scroll
        // is what flashed the panel every few seconds, at any drag speed.
        if (touch_point().down) {
            batt_next = now + 400;
        } else {
            batt_next = now + period;
            read_battery();
            battery_sleep = screen_off && !on_external_power();
        }
    }

    // On battery the dark screen ignores the panel. GPIO keys already called input_mark().
    if (!battery_sleep && touch_point().down) input_mark();
    uint32_t idle = now - input_last_ms();

    if (screen_off && idle < 300) {
        screen_off = false;
        swallow = true;
        wake_edge = true;
        set_cpu_mhz(240);
        power_backlight(bl_active);
        Serial.println("Wake from idle");
        return;
    }

    if (busy) {
        if (bl_level != bl_active) power_backlight(bl_active);
        set_cpu_mhz(240);
        return;
    }

    if (idle > off_after) {
        if (!screen_off) {
            screen_off = true;
            power_backlight(0);
            Serial.println(on_external_power() ? "Screen off" : "Screen off, battery");
        }
        set_cpu_mhz(on_external_power() ? 240 : 80);
        return;
    }
    if (idle > dim_after) {
        if (bl_level != bl_dimmed) power_backlight(bl_dimmed);
        set_cpu_mhz(240);
        return;
    }
    if (bl_level != bl_active) power_backlight(bl_active);
    set_cpu_mhz(240);
}
