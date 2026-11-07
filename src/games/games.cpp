#include "games/games.h"

#include "board_config.h"
#include "hw/audio.h"
#include "hw/input.h"
#include "hw/touch.h"
#include "pet/pet.h"
#include "ui/font_ru.h"
#include "ui/ui.h"
#include "ui/ui_common.h"
#include "util.h"

#include <Arduino.h>
#include <stdio.h>

enum { PH_PLAY = 0, PH_RESULT = 1 };

static bool live = false;
static int gid = 0;
static int phase = PH_PLAY;
static bool paid = false;
static bool result_made = false;
static int score = 0;
static int combo = 0;
static int best_combo = 0;
static int payout = 0;
static uint32_t started = 0;
static uint32_t acc = 0;
static lv_obj_t *root = nullptr;
static lv_obj_t *hud = nullptr;
static lv_obj_t *actor = nullptr;
static lv_obj_t *bits[5] = {};
static int ax = 40;
static int ay = 160;
static int react_round = 0;
static int react_phase = 0;
static uint32_t react_at = 0;
static uint32_t react_go = 0;
static uint8_t seq[8];
static int seq_len = 0;
static int seq_pos = 0;
static bool showing = false;
static bool listen = false;
static int drop_y[5];
static int drop_lane[5];
static bool drop_on[5];
static int basket = 111;
static uint32_t spawn_acc = 0;
static lv_color_t tile_col[4];

static uint32_t game_epoch = 0;
static uint32_t live_epoch = 0;

static void forget(lv_event_t *e)
{
    uint32_t ep = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    if (ep != live_epoch) return;
    live = false;
    root = hud = actor = nullptr;
    for (int i = 0; i < 5; i++) bits[i] = nullptr;
}

static void grant()
{
    if (paid) return;
    paid = true;
    int coins = 0;
    if (gid == GAME_TAP) coins = score + best_combo / 2;
    else if (gid == GAME_REACT) coins = score / 6;
    else if (gid == GAME_MEMORY) coins = score * 5;
    else coins = score;
    if (coins > 40) coins = 40;
    bool weak = pet_stat(pet_active(), STAT_HEALTH) < 15 || pet_stat(pet_active(), STAT_ENERGY) < 12;
    if (weak) coins /= 2;
    if (coins < 1) coins = 1;
    payout = coins;
    bool up = pet_reward(coins, weak ? 4 : 8, weak ? 4 : 8, coins);
    audio_coin();
    if (up) ui_toast("Новый уровень");
}

static void finish_play()
{
    if (phase == PH_RESULT) return;
    phase = PH_RESULT;
}

bool game_on_back()
{
    if (!live) return false;
    if (phase == PH_PLAY) {
        finish_play();
        return true;
    }
    return false;
}

static void build_result()
{
    if (!root || result_made) return;
    result_made = true;
    grant();
    lv_obj_t *card = lv_obj_create(root);
    lv_obj_set_size(card, 196, 168);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(card, lv_color_hex(col.card), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(col.line), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    char buf[64];
    if (payout > 0) snprintf(buf, sizeof(buf), "Счёт %d\n+%d монет", score, payout);
    else snprintf(buf, sizeof(buf), "Счёт %d\nПока без монет", score);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 170);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 16);
    ui_button(card, 10, 100, 80, 44, "Ещё", [](lv_event_t *e) {
        (void)e;
        ui_play(gid);
    }, NULL);
    ui_button(card, 100, 100, 80, 44, "Меню", [](lv_event_t *e) {
        (void)e;
        ui_show(SCR_GAMES);
    }, NULL);
}

static void set_hud(const char *text)
{
    ui_set_text(hud, text);
}

static bool pressed_in_play()
{
    TouchPoint p = touch_point();
    bool tap = touch_take_press();
    bool key = input_take_action();
    if (tap && p.y < 52) tap = false;
    return tap || key;
}

static void move_target()
{
    ax = random(8, 150);
    ay = random(110, 330);
    if (actor) lv_obj_set_pos(actor, ax, ay);
}

static void tick_tap(uint32_t now)
{
    if (now - started >= 15000) {
        finish_play();
        return;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "Тапы %d   %lus", score, (unsigned long)((15000 - (now - started)) / 1000));
    set_hud(buf);
    TouchPoint p = touch_point();
    bool tap = touch_take_press();
    bool key = input_take_action();
    if (tap && p.y < 52) tap = false;
    bool hit = key;
    if (tap && p.x >= ax && p.x < ax + 64 && p.y >= ay && p.y < ay + 64) hit = true;
    if (!hit) {
        if (now - started > 0 && (now - acc) > 700) combo = 0;
        return;
    }
    score++;
    if (now - acc < 450) combo++;
    else combo = 1;
    acc = now;
    if (combo > best_combo) best_combo = combo;
    audio_ok();
    move_target();
}

static void tick_react(uint32_t now)
{
    bool press = pressed_in_play();
    if (react_phase == 0) {
        set_hud("Жди зелёный");
        if (actor) {
            lv_obj_set_style_bg_color(actor, lv_color_hex(col.track), 0);
            lv_obj_t *word = lv_obj_get_child(actor, 0);
            if (word) lv_obj_set_style_text_color(word, lv_color_hex(col.ink), 0);
        }
        if (press) {
            audio_fail();
            set_hud("Рано");
            react_round++;
            react_phase = 2;
            react_at = now + 500;
            return;
        }
        if (now >= react_at) {
            react_phase = 1;
            react_go = now;
            if (actor) {
                lv_obj_set_style_bg_color(actor, lv_color_hex(col.mint), 0);
                lv_obj_t *word = lv_obj_get_child(actor, 0);
                if (word) lv_obj_set_style_text_color(word, lv_color_hex(col.ink), 0);
            }
            set_hud("ЖМИ");
        }
        return;
    }
    if (react_phase == 1) {
        if (press) {
            int ms = (int)(now - react_go);
            int pts = (500 - ms) / 5;
            if (pts < 0) pts = 0;
            score += pts;
            audio_ok();
            char buf[32];
            snprintf(buf, sizeof(buf), "%d мс", ms);
            set_hud(buf);
            react_round++;
            react_phase = 2;
            react_at = now + 450;
            return;
        }
        if (now - react_go > 900) {
            audio_fail();
            set_hud("Мимо");
            react_round++;
            react_phase = 2;
            react_at = now + 450;
        }
        return;
    }
    if (now < react_at) return;
    if (react_round >= 5) {
        finish_play();
        return;
    }
    react_phase = 0;
    react_at = now + (uint32_t)random(700, 2000);
    set_hud("Жди зелёный");
}

static void light_tiles(int lit)
{
    for (int i = 0; i < 4; i++) {
        if (!bits[i]) continue;
        bool on = showing && i == lit;
        lv_obj_set_style_bg_color(bits[i], on ? lv_color_hex(0xFFFFFF) : tile_col[i], 0);
    }
}

static void begin_show()
{
    showing = true;
    listen = false;
    acc = 0;
}

static void on_tile(lv_event_t *e)
{
    if (!live || !listen || phase != PH_PLAY) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id != seq[seq_pos]) {
        audio_fail();
        listen = false;
        finish_play();
        return;
    }
    audio_click();
    seq_pos++;
    if (seq_pos < seq_len) return;
    score++;
    listen = false;
    if (seq_len >= 8) {
        finish_play();
        return;
    }
    seq[seq_len++] = (uint8_t)random(0, 4);
    begin_show();
}

static void tick_memory(uint32_t dt)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "Ряд %d", score);
    set_hud(buf);
    if (!showing) return;
    acc += dt;
    int slot = (int)(acc / 520);
    int local = (int)(acc % 520);
    if (slot >= seq_len) {
        showing = false;
        listen = true;
        seq_pos = 0;
        light_tiles(-1);
        return;
    }
    light_tiles(local < 340 ? seq[slot] : -1);
}

static void tick_chase(uint32_t now, uint32_t dt)
{
    if (now - started >= 20000) {
        finish_play();
        return;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "Поймано %d   %lus", score, (unsigned long)((20000 - (now - started)) / 1000));
    set_hud(buf);
    TouchPoint p = touch_point();
    if (p.down && p.y > 70) basket = (int)clampv<int>(p.x, 46, 176);
    if (actor) lv_obj_set_x(actor, basket - 36);
    spawn_acc += dt;
    int spawn_every = 680;
    if (now - started > 8000) spawn_every = 480;
    if (spawn_acc >= (uint32_t)spawn_every) {
        spawn_acc = 0;
        for (int i = 0; i < 5; i++) {
            if (drop_on[i]) continue;
            drop_on[i] = true;
            drop_lane[i] = random(0, 3);
            drop_y[i] = 70;
            break;
        }
    }
    int step = (int)(dt / 6) + 2;
    for (int i = 0; i < 5; i++) {
        if (!drop_on[i] || !bits[i]) continue;
        drop_y[i] += step;
        int x = 16 + drop_lane[i] * 64;
        lv_obj_set_pos(bits[i], x, drop_y[i]);
        lv_obj_clear_flag(bits[i], LV_OBJ_FLAG_HIDDEN);
        int cx = x + 16;
        if (drop_y[i] > 378 && drop_y[i] < 430 && (cx > basket - 42) && (cx < basket + 42)) {
            drop_on[i] = false;
            score++;
            audio_ok();
            lv_obj_add_flag(bits[i], LV_OBJ_FLAG_HIDDEN);
        } else if (drop_y[i] > 450) {
            drop_on[i] = false;
            lv_obj_add_flag(bits[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void style_plain(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static const char *game_title(int id)
{
    if (id == GAME_TAP) return "Тап";
    if (id == GAME_REACT) return "Реакция";
    if (id == GAME_MEMORY) return "Память";
    return "Лови";
}

lv_obj_t *games_menu_create()
{
    lv_obj_t *scr = ui_screen();
    ui_back_bar(scr, "Игры");
    ui_label(scr, 16, 58, "Монеты за ловкость", &font_ru_16, col.ink);
    ui_button(scr, 16, 100, 190, 64, "Тап-тап", [](lv_event_t *e) {
        (void)e;
        ui_play(GAME_TAP);
    }, NULL);
    ui_button(scr, 16, 176, 190, 64, "Реакция", [](lv_event_t *e) {
        (void)e;
        ui_play(GAME_REACT);
    }, NULL);
    ui_button(scr, 16, 252, 190, 64, "Память", [](lv_event_t *e) {
        (void)e;
        ui_play(GAME_MEMORY);
    }, NULL);
    ui_button(scr, 16, 328, 190, 64, "Лови еду", [](lv_event_t *e) {
        (void)e;
        ui_play(GAME_CHASE);
    }, NULL);
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "\"Действие\" тоже жмёт");
    lv_obj_set_pos(hint, 16, 410);
    lv_obj_set_width(hint, 190);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(hint, &font_ru_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(col.ink), 0);
    return scr;
}

lv_obj_t *game_create(int id)
{
    gid = id;
    phase = PH_PLAY;
    paid = false;
    result_made = false;
    score = 0;
    combo = 0;
    best_combo = 0;
    payout = 0;
    started = millis();
    acc = started;
    live = true;
    react_round = 0;
    react_phase = 0;
    react_at = started + (uint32_t)random(800, 1800);
    seq_len = 2;
    seq_pos = 0;
    showing = false;
    listen = false;
    basket = 111;
    spawn_acc = 0;
    for (int i = 0; i < 5; i++) {
        bits[i] = nullptr;
        drop_on[i] = false;
        drop_y[i] = -40;
    }
    for (int i = 0; i < 8; i++) seq[i] = (uint8_t)random(0, 4);
    if (id == GAME_MEMORY) begin_show();

    root = ui_screen();
    live_epoch = ++game_epoch;
    lv_obj_add_event_cb(root, forget, LV_EVENT_DELETE, (void *)(intptr_t)live_epoch);
    touch_take_press();
    input_take_action();
    ui_back_bar(root, game_title(id));
    hud = lv_label_create(root);
    lv_obj_set_pos(hud, 16, 52);
    lv_obj_set_style_text_color(hud, lv_color_hex(col.ink), 0);
    lv_label_set_text(hud, "...");

    if (id == GAME_TAP) {
        actor = lv_btn_create(root);
        lv_obj_set_size(actor, 64, 64);
        style_plain(actor, col.card, 32);
        lv_obj_set_style_border_width(actor, 3, 0);
        lv_obj_set_style_border_color(actor, lv_color_hex(col.line), 0);
        lv_obj_t *heart = lv_label_create(actor);
        lv_label_set_text(heart, "+");
        lv_obj_set_style_text_color(heart, lv_color_hex(col.ink), 0);
        lv_obj_center(heart);
        move_target();
    } else if (id == GAME_REACT) {
        actor = lv_obj_create(root);
        lv_obj_set_pos(actor, 16, 120);
        lv_obj_set_size(actor, 190, 220);
        style_plain(actor, col.track, 24);
        lv_obj_t *word = lv_label_create(actor);
        lv_label_set_text(word, "ЖМИ");
        lv_obj_set_style_text_font(word, &font_ru_22, 0);
        lv_obj_set_style_text_color(word, lv_color_hex(col.ink), 0);
        lv_obj_center(word);
    } else if (id == GAME_MEMORY) {
        tile_col[0] = lv_color_hex(col.hot);
        tile_col[1] = lv_color_hex(col.gold);
        tile_col[2] = lv_color_hex(col.mint);
        tile_col[3] = lv_color_hex(col.lilac);
        for (int i = 0; i < 4; i++) {
            bits[i] = lv_btn_create(root);
            lv_obj_set_pos(bits[i], 16 + (i % 2) * 100, 100 + (i / 2) * 100);
            lv_obj_set_size(bits[i], 90, 90);
            lv_obj_set_style_bg_color(bits[i], tile_col[i], 0);
            lv_obj_set_style_radius(bits[i], 18, 0);
            lv_obj_set_style_shadow_width(bits[i], 0, 0);
            lv_obj_set_style_border_width(bits[i], 0, 0);
            lv_obj_add_event_cb(bits[i], on_tile, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    } else {
        actor = lv_obj_create(root);
        lv_obj_set_size(actor, 78, 22);
        lv_obj_set_pos(actor, basket - 36, 404);
        style_plain(actor, col.card, 12);
        lv_obj_set_style_border_width(actor, 3, 0);
        lv_obj_set_style_border_color(actor, lv_color_hex(col.line), 0);
        uint32_t colors[3] = {col.gold, col.mint, col.lilac};
        for (int i = 0; i < 5; i++) {
            bits[i] = lv_obj_create(root);
            lv_obj_set_size(bits[i], 28, 28);
            style_plain(bits[i], colors[i % 3], 14);
            lv_obj_add_flag(bits[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    return root;
}

void game_tick(uint32_t dt)
{
    if (!live) return;
    uint32_t now = millis();
    if (phase == PH_RESULT) {
        build_result();
        return;
    }
    if (gid == GAME_TAP) tick_tap(now);
    else if (gid == GAME_REACT) tick_react(now);
    else if (gid == GAME_MEMORY) tick_memory(dt);
    else tick_chase(now, dt);
    if (phase == PH_RESULT) build_result();
}
