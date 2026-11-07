#pragma once

#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)
struct PetSlot {
    int8_t hunger;
    int8_t happy;
    int8_t health;
    int8_t energy;
    int8_t clean;
    uint8_t head;
    uint8_t outfit;
    uint8_t extra;
    uint8_t sleeping;
    uint8_t pad;
    uint16_t level;
    uint32_t xp;
    uint32_t age_sec;
};

struct SaveData {
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
    uint8_t trans_idx; // screen transition style
    uint8_t food[8];
    uint32_t coins;
    uint32_t clothes;
    uint32_t last_unix;
    PetSlot pet[2];
    uint8_t pace_idx; // 0 = 30 s, 1 = 5 min, 2 = 30 min
    uint8_t btn[3];   // action for BOOT, GPIO 12, GPIO 16
    uint32_t crc;
};
#pragma pack(pop)

void save_init();
SaveData &save_data();
void save_mark();
void save_flush();
void save_tick(uint32_t now);
void save_reset();
uint32_t save_clock_now();
uint8_t save_touch_mode();
void save_set_touch_mode(uint8_t mode);
uint8_t save_theme();
void save_set_theme(uint8_t mode);
uint8_t save_dim_idx();
void save_set_dim_idx(uint8_t idx);
uint8_t save_off_idx();
void save_set_off_idx(uint8_t idx);
uint8_t save_bl_idx();
void save_set_bl_idx(uint8_t idx);
uint8_t save_led_flash();
void save_set_led_flash(uint8_t on);
uint8_t save_show_fps();
void save_set_show_fps(uint8_t on);
uint8_t save_trans_idx();
void save_set_trans_idx(uint8_t idx);
uint8_t save_pace_idx();
void save_set_pace_idx(uint8_t idx);
const char *save_pace_name(uint8_t idx);
uint16_t save_pace_sec();
enum BtnAct : uint8_t {
    BTN_ACT_BACK = 0,
    BTN_ACT_NEXT,
    BTN_ACT_ACTION,
    BTN_ACT_FEED,
    BTN_ACT_GAMES,
    BTN_ACT_SHOP,
    BTN_ACT_STATUS,
    BTN_ACT_SETTINGS,
    BTN_ACT_HOME,
    BTN_ACT_SLEEP,
    BTN_ACT_NONE,
    BTN_ACT_COUNT
};
uint8_t save_btn_act(uint8_t key);
void save_set_btn_act(uint8_t key, uint8_t act);
const char *save_btn_act_name(uint8_t act);
const char *save_btn_key_name(uint8_t key);
bool save_export_sd();
bool save_import_latest_sd();
// Packed SaveData blob for HTTP / radio transfer (flushes first).
bool save_copy_out(uint8_t *dst, size_t dst_n, size_t *out_n);
bool save_import_bytes(const uint8_t *buf, size_t n);
