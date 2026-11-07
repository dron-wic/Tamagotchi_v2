#include "storage/save.h"

#include "hw/sdcard.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

static const uint32_t SAVE_MAGIC = 0x54414D41;
static const uint16_t SAVE_VERSION = 5;

// Version 4 is the previous packed layout, without button actions.
#pragma pack(push, 1)
struct SaveDataV4 {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint8_t active;
    uint8_t started;
    uint8_t touch_mode;
    uint8_t theme;
    uint8_t dim_idx;
    uint8_t off_idx;
    uint8_t bl_idx;
    uint8_t led_flash;
    uint8_t show_fps;
    uint8_t trans_idx;
    uint8_t food[8];
    uint32_t coins;
    uint32_t clothes;
    uint32_t last_unix;
    PetSlot pet[2];
    uint8_t pace_idx;
    uint32_t crc;
};
#pragma pack(pop)

// Version 3 is the previous packed layout, without pace_idx.
#pragma pack(push, 1)
struct SaveDataV3 {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint8_t active;
    uint8_t started;
    uint8_t touch_mode;
    uint8_t theme;
    uint8_t dim_idx;
    uint8_t off_idx;
    uint8_t bl_idx;
    uint8_t led_flash;
    uint8_t show_fps;
    uint8_t trans_idx;
    uint8_t food[8];
    uint32_t coins;
    uint32_t clothes;
    uint32_t last_unix;
    PetSlot pet[2];
    uint32_t crc;
};
#pragma pack(pop)

static_assert(sizeof(SaveData) == 86, "SaveData v5");
static_assert(sizeof(SaveDataV4) == 83, "SaveData v4");
static_assert(sizeof(SaveDataV3) == 82, "SaveData v3");

// Version 1 had no theme or power preferences. Kept so an existing
// pet is not wiped when the settings fields were added.
#pragma pack(push, 1)
struct SaveDataV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint8_t active;
    uint8_t started;
    uint8_t touch_mode;
    uint8_t pad;
    uint8_t food[8];
    uint32_t coins;
    uint32_t clothes;
    uint32_t last_unix;
    PetSlot pet[2];
    uint32_t crc;
};
#pragma pack(pop)

static SaveData data;
static bool dirty = false;
static uint32_t dirty_since = 0;
static bool fs_ok = false;

static uint32_t crc32(const uint8_t *bytes, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= bytes[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~c;
}

static void stamp()
{
    data.magic = SAVE_MAGIC;
    data.version = SAVE_VERSION;
    data.bytes = sizeof(SaveData);
    data.crc = crc32((const uint8_t *)&data, offsetof(SaveData, crc));
}

static bool valid(const SaveData &in)
{
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) return false;
    if (in.bytes != sizeof(SaveData)) return false;
    SaveData copy = in;
    uint32_t expect = crc32((const uint8_t *)&copy, offsetof(SaveData, crc));
    return expect == in.crc;
}

static void default_prefs()
{
    data.theme = 0;
    data.dim_idx = 2;
    data.off_idx = 2;
    data.bl_idx = 2;
    data.led_flash = 1;
    data.show_fps = 1;
    data.trans_idx = 0;
}

static bool valid_v1(const SaveDataV1 &in)
{
    if (in.magic != SAVE_MAGIC || in.version != 1) return false;
    if (in.bytes != sizeof(SaveDataV1)) return false;
    SaveDataV1 copy = in;
    uint32_t expect = crc32((const uint8_t *)&copy, offsetof(SaveDataV1, crc));
    return expect == in.crc;
}

static void default_btns()
{
    data.btn[0] = BTN_ACT_BACK;
    data.btn[1] = BTN_ACT_ACTION;
    data.btn[2] = BTN_ACT_NEXT;
}

static bool valid_v4(const SaveDataV4 &in)
{
    if (in.magic != SAVE_MAGIC || in.version != 4) return false;
    if (in.bytes != sizeof(SaveDataV4)) return false;
    SaveDataV4 copy = in;
    uint32_t expect = crc32((const uint8_t *)&copy, offsetof(SaveDataV4, crc));
    return expect == in.crc;
}

static void adopt_v4(const SaveDataV4 &old)
{
    memset(&data, 0, sizeof(data));
    data.active = old.active;
    data.started = old.started;
    data.touch_mode = old.touch_mode;
    data.theme = old.theme;
    data.dim_idx = old.dim_idx;
    data.off_idx = old.off_idx;
    data.bl_idx = old.bl_idx;
    data.led_flash = old.led_flash;
    data.show_fps = old.show_fps;
    data.trans_idx = old.trans_idx;
    memcpy(data.food, old.food, sizeof(data.food));
    data.coins = old.coins;
    data.clothes = old.clothes;
    data.last_unix = old.last_unix;
    data.pet[0] = old.pet[0];
    data.pet[1] = old.pet[1];
    data.pace_idx = old.pace_idx;
    default_btns();
    data.version = 4;
}

static bool valid_v3(const SaveDataV3 &in)
{
    if (in.magic != SAVE_MAGIC || in.version != 3) return false;
    if (in.bytes != sizeof(SaveDataV3)) return false;
    SaveDataV3 copy = in;
    uint32_t expect = crc32((const uint8_t *)&copy, offsetof(SaveDataV3, crc));
    return expect == in.crc;
}

static void adopt_v3(const SaveDataV3 &old)
{
    memset(&data, 0, sizeof(data));
    data.active = old.active;
    data.started = old.started;
    data.touch_mode = old.touch_mode;
    data.theme = old.theme;
    data.dim_idx = old.dim_idx;
    data.off_idx = old.off_idx;
    data.bl_idx = old.bl_idx;
    data.led_flash = old.led_flash;
    data.show_fps = old.show_fps;
    data.trans_idx = old.trans_idx;
    memcpy(data.food, old.food, sizeof(data.food));
    data.coins = old.coins;
    data.clothes = old.clothes;
    data.last_unix = old.last_unix;
    data.pet[0] = old.pet[0];
    data.pet[1] = old.pet[1];
    data.pace_idx = 0;
    default_btns();
    data.version = 3;
}

static void adopt_v1(const SaveDataV1 &old)
{
    memset(&data, 0, sizeof(data));
    data.active = old.active;
    data.started = old.started;
    data.touch_mode = old.touch_mode;
    memcpy(data.food, old.food, sizeof(data.food));
    data.coins = old.coins;
    data.clothes = old.clothes;
    data.last_unix = old.last_unix;
    data.pet[0] = old.pet[0];
    data.pet[1] = old.pet[1];
    default_prefs();
    default_btns();
    data.version = 1;
}

static void fill_slot(PetSlot &p)
{
    memset(&p, 0, sizeof(p));
    p.hunger = 72;
    p.happy = 74;
    p.health = 92;
    p.energy = 84;
    p.clean = 86;
    p.level = 1;
}

static bool take_blob(const uint8_t *buf, size_t n)
{
    if (n >= sizeof(SaveData)) {
        SaveData in;
        memcpy(&in, buf, sizeof(in));
        if (valid(in)) {
            data = in;
            return true;
        }
    }
    if (n >= sizeof(SaveDataV4)) {
        SaveDataV4 old;
        memcpy(&old, buf, sizeof(old));
        if (valid_v4(old)) {
            adopt_v4(old);
            return true;
        }
    }
    if (n >= sizeof(SaveDataV3)) {
        SaveDataV3 old;
        memcpy(&old, buf, sizeof(old));
        if (valid_v3(old)) {
            adopt_v3(old);
            return true;
        }
    }
    if (n >= sizeof(SaveDataV1)) {
        SaveDataV1 old;
        memcpy(&old, buf, sizeof(old));
        if (valid_v1(old)) {
            adopt_v1(old);
            return true;
        }
    }
    return false;
}

static bool load_nvs()
{
    Preferences prefs;
    if (!prefs.begin("tama", true)) return false;
    uint8_t buf[sizeof(SaveData)];
    size_t n = prefs.getBytes("blob", buf, sizeof(buf));
    prefs.end();
    return take_blob(buf, n);
}

static bool load_fs()
{
    if (!fs_ok) return false;
    File f = LittleFS.open("/save.bin", "r");
    if (!f) return false;
    uint8_t buf[sizeof(SaveData)];
    size_t n = f.read(buf, sizeof(buf));
    f.close();
    return take_blob(buf, n);
}

static void write_both()
{
    stamp();
    Preferences prefs;
    if (prefs.begin("tama", false)) {
        prefs.putBytes("blob", &data, sizeof(data));
        prefs.end();
    }
    if (fs_ok) {
        File f = LittleFS.open("/save.bin", "w");
        if (f) {
            f.write((const uint8_t *)&data, sizeof(data));
            f.close();
        }
    }
    dirty = false;
}

void save_reset()
{
    memset(&data, 0, sizeof(data));
    default_prefs();
    data.touch_mode = 0;
    data.coins = 30;
    data.food[0] = 2;
    fill_slot(data.pet[0]);
    fill_slot(data.pet[1]);
    data.active = 0;
    data.started = 0;
    default_btns();
    write_both();
    Serial.println("Save reset");
}

void save_init()
{
    fs_ok = LittleFS.begin(true, "/littlefs", 8, "spiffs");
    if (!fs_ok) Serial.println("LittleFS mount failed");
    bool loaded = load_nvs() || load_fs();
    if (!loaded) {
        memset(&data, 0, sizeof(data));
        save_reset();
    } else {
        // v3 drops touch calibration and restores defaults once so a
        // broken mode cannot stick after an update.
        bool wipe_prefs = data.version < 3;
        data.touch_mode = 0;
        if (wipe_prefs) default_prefs();
        write_both();
    }
    Serial.printf("Save coins=%lu pet=%u started=%u touch=0\n",
                  (unsigned long)data.coins, data.active, data.started);
}

SaveData &save_data()
{
    return data;
}

void save_mark()
{
    if (!dirty) dirty_since = millis();
    dirty = true;
}

void save_flush()
{
    uint32_t now = save_clock_now();
    if (now) data.last_unix = now;
    write_both();
}

void save_tick(uint32_t now)
{
    if (!dirty) return;
    if ((int32_t)(now - dirty_since) > 4000) save_flush();
}

uint32_t save_clock_now()
{
    time_t t = time(nullptr);
    if (t > 1700000000) return (uint32_t)t;
    return 0;
}

uint8_t save_touch_mode()
{
    return data.touch_mode & 7;
}

void save_set_touch_mode(uint8_t mode)
{
    data.touch_mode = mode & 7;
    save_mark();
}

static uint8_t clamp_set(uint8_t value, uint8_t max_idx)
{
    return value <= max_idx ? value : max_idx;
}

uint8_t save_theme()
{
    return data.theme & 1;
}
void save_set_theme(uint8_t mode)
{
    data.theme = mode & 1;
    save_mark();
}
uint8_t save_dim_idx()
{
    return clamp_set(data.dim_idx, 5);
}
void save_set_dim_idx(uint8_t idx)
{
    data.dim_idx = clamp_set(idx, 5);
    save_mark();
}
uint8_t save_off_idx()
{
    return clamp_set(data.off_idx, 4);
}
void save_set_off_idx(uint8_t idx)
{
    data.off_idx = clamp_set(idx, 4);
    save_mark();
}
uint8_t save_bl_idx()
{
    return clamp_set(data.bl_idx, 3);
}
void save_set_bl_idx(uint8_t idx)
{
    data.bl_idx = clamp_set(idx, 3);
    save_mark();
}
uint8_t save_led_flash()
{
    return data.led_flash ? 1 : 0;
}
void save_set_led_flash(uint8_t on)
{
    data.led_flash = on ? 1 : 0;
    save_mark();
}
uint8_t save_show_fps()
{
    return data.show_fps ? 1 : 0;
}
void save_set_show_fps(uint8_t on)
{
    data.show_fps = on ? 1 : 0;
    save_mark();
}
uint8_t save_trans_idx()
{
    // Old firmware stored "Центр" as 2 and "Шторка" as 3; map both to
    // the stable styles (2 was crashy zoom — treat as curtain / clamp).
    uint8_t v = data.trans_idx;
    if (v == 3) v = 2;
    return clamp_set(v, 2);
}
void save_set_trans_idx(uint8_t idx)
{
    data.trans_idx = clamp_set(idx, 2);
    save_mark();
}

static const uint16_t PACE_SEC[] = {30, 300, 1800};
static const char *PACE_NAME[] = {"30 с", "5 мин", "30 мин"};

uint8_t save_pace_idx()
{
    return clamp_set(data.pace_idx, 2);
}
void save_set_pace_idx(uint8_t idx)
{
    data.pace_idx = clamp_set(idx, 2);
    save_mark();
}
const char *save_pace_name(uint8_t idx)
{
    return PACE_NAME[clamp_set(idx, 2)];
}
uint16_t save_pace_sec()
{
    return PACE_SEC[save_pace_idx()];
}

static const char *BTN_ACT_NAME[] = {
    "Назад", "Дальше", "Действие", "Корм", "Игры", "Магазин",
    "Статус", "Настройки", "Домой", "Сон", "Нет",
};
static_assert(sizeof(BTN_ACT_NAME) / sizeof(BTN_ACT_NAME[0]) == BTN_ACT_COUNT, "button action names");
static const char *BTN_KEY_NAME[] = {"BOOT", "GPIO 12", "GPIO 16"};

uint8_t save_btn_act(uint8_t key)
{
    if (key > 2) return BTN_ACT_NONE;
    uint8_t v = data.btn[key];
    return v < BTN_ACT_COUNT ? v : BTN_ACT_NONE;
}
void save_set_btn_act(uint8_t key, uint8_t act)
{
    if (key > 2) return;
    data.btn[key] = act < BTN_ACT_COUNT ? act : BTN_ACT_NONE;
    save_mark();
}
const char *save_btn_act_name(uint8_t act)
{
    return BTN_ACT_NAME[act < BTN_ACT_COUNT ? act : BTN_ACT_NONE];
}
const char *save_btn_key_name(uint8_t key)
{
    return key > 2 ? "" : BTN_KEY_NAME[key];
}

bool save_export_sd()
{
    if (!sd_ok()) return false;
    if (!sd_ensure_dir("/backup")) return false;
    save_flush();

    char path[48];
    uint32_t stamp_t = data.last_unix ? data.last_unix : (1000000000u + millis() / 1000u);
    snprintf(path, sizeof(path), "/backup/save_%010lu.bin", (unsigned long)stamp_t);
    bool ok = sd_write_file(path, &data, sizeof(data));
    Serial.printf("Save export %s %s\n", path, ok ? "ok" : "fail");
    return ok;
}

bool save_import_latest_sd()
{
    if (!sd_ok()) return false;
    char path[64];
    if (!sd_newest_backup(path, sizeof(path))) return false;

    uint8_t buf[sizeof(SaveData)];
    size_t n = 0;
    if (!sd_read_file(path, buf, sizeof(buf), &n)) return false;
    if (!take_blob(buf, n)) return false;
    write_both();
    Serial.printf("Save import %s ok\n", path);
    return true;
}

bool save_copy_out(uint8_t *dst, size_t dst_n, size_t *out_n)
{
    if (!dst || dst_n < sizeof(SaveData)) return false;
    save_flush();
    memcpy(dst, &data, sizeof(data));
    if (out_n) *out_n = sizeof(data);
    return true;
}

bool save_import_bytes(const uint8_t *buf, size_t n)
{
    if (!take_blob(buf, n)) return false;
    write_both();
    return true;
}
