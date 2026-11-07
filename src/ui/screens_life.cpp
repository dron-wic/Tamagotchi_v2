#include "ui/screens.h"

#include "assets/icons.h"
#include "board_config.h"
#include "hw/audio.h"
#include "hw/display.h"
#include "hw/net.h"
#include "hw/power.h"
#include "pet/pet.h"
#include "pet/render.h"
#include "storage/save.h"
#include "ui/font_ru.h"
#include "ui/ui.h"
#include "ui/ui_common.h"
#include "util.h"

#include <Arduino.h>
#include <stdio.h>

struct Stage {
    bool live;
    lv_obj_t *canvas;
    uint16_t *buf;
    int w;
    int h;
    int seen;
};

static void stage_close(Stage &s)
{
    s.live = false;
    s.canvas = nullptr;
    if (s.buf) ps_free(s.buf);
    s.buf = nullptr;
}

static void stage_open(lv_obj_t *parent, Stage &s, int x, int y, int w, int h)
{
    s.live = true;
    s.w = w;
    s.h = h;
    s.seen = -1;
    s.buf = (uint16_t *)ps_alloc((size_t)w * h * 2);
    s.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(s.canvas, x, y);
    lv_obj_clear_flag(s.canvas, LV_OBJ_FLAG_CLICKABLE);
    if (s.buf) {
        uint16_t bg = ui_bg565();
        int n = w * h;
        for (int i = 0; i < n; i++) s.buf[i] = bg;
        lv_canvas_set_buffer(s.canvas, s.buf, w, h, LV_IMG_CF_TRUE_COLOR);
    }
}

static void stage_draw(Stage &s, const PetView &view)
{
    if (!s.live || !s.buf || !s.canvas) return;
    if (view.frame == s.seen) return;
    s.seen = view.frame;
    PetView copy = view;
    copy.bg = ui_bg565();
    pet_render(s.buf, s.w, s.h, copy);
    lv_obj_invalidate(s.canvas);
}

static Stage home_stage;
static lv_obj_t *home_scr = nullptr;
static lv_obj_t *home_mood;
static lv_obj_t *home_clock;
static lv_obj_t *home_coins;
static lv_obj_t *home_fps;
static lv_obj_t *home_batt_lbl;
static lv_obj_t *home_batt_fill;
static lv_obj_t *home_batt_bolt;
static lv_obj_t *home_hunger;
static lv_obj_t *home_happy;

static void on_home_del(lv_event_t *e)
{
    uint16_t *buf = (uint16_t *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) != home_scr) {
        if (buf && buf != home_stage.buf) ps_free(buf);
        return;
    }
    stage_close(home_stage);
    home_scr = nullptr;
    home_mood = home_clock = home_coins = home_fps = nullptr;
    home_batt_lbl = home_batt_fill = home_batt_bolt = nullptr;
    home_hunger = home_happy = nullptr;
}

static void on_feed_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_FEED);
}
static void on_play_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_GAMES);
}
static void on_shop_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_SHOP);
}
static void on_status_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_STATUS);
}
static void on_settings_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_SETTINGS);
}
static void on_camera_click(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_CAMERA);
}

lv_obj_t *screen_home_create()
{
    lv_obj_t *scr = ui_screen();
    lv_obj_t *name = lv_label_create(scr);
    lv_label_set_text(name, pet_name(pet_active()));
    lv_obj_set_style_text_font(name, &font_ru_22, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(col.ink), 0);
    lv_obj_set_pos(name, 8, 6);
    lv_obj_set_width(name, 96);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    ui_button(scr, 108, 4, 106, 34, "Настройки", on_settings_click, NULL);

    // Battery body + tip + fill + charge bolt.
    lv_obj_t *batt_body = lv_obj_create(scr);
    lv_obj_set_pos(batt_body, 8, 42);
    lv_obj_set_size(batt_body, 28, 14);
    lv_obj_set_style_radius(batt_body, 3, 0);
    lv_obj_set_style_border_width(batt_body, 2, 0);
    lv_obj_set_style_border_color(batt_body, lv_color_hex(col.ink), 0);
    lv_obj_set_style_bg_color(batt_body, lv_color_hex(col.bg), 0);
    lv_obj_set_style_pad_all(batt_body, 0, 0);
    lv_obj_set_style_shadow_width(batt_body, 0, 0);
    lv_obj_clear_flag(batt_body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *batt_tip = lv_obj_create(scr);
    lv_obj_set_pos(batt_tip, 36, 45);
    lv_obj_set_size(batt_tip, 3, 8);
    lv_obj_set_style_radius(batt_tip, 1, 0);
    lv_obj_set_style_border_width(batt_tip, 0, 0);
    lv_obj_set_style_bg_color(batt_tip, lv_color_hex(col.ink), 0);
    lv_obj_set_style_pad_all(batt_tip, 0, 0);
    lv_obj_set_style_shadow_width(batt_tip, 0, 0);
    lv_obj_clear_flag(batt_tip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    home_batt_fill = lv_obj_create(batt_body);
    lv_obj_set_pos(home_batt_fill, 2, 2);
    lv_obj_set_size(home_batt_fill, 1, 10);
    lv_obj_set_style_radius(home_batt_fill, 1, 0);
    lv_obj_set_style_border_width(home_batt_fill, 0, 0);
    lv_obj_set_style_bg_color(home_batt_fill, lv_color_hex(col.mint), 0);
    lv_obj_set_style_pad_all(home_batt_fill, 0, 0);
    lv_obj_set_style_shadow_width(home_batt_fill, 0, 0);
    lv_obj_clear_flag(home_batt_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    home_batt_bolt = lv_label_create(scr);
    lv_obj_set_pos(home_batt_bolt, 16, 40);
    lv_obj_set_style_text_color(home_batt_bolt, lv_color_hex(col.gold), 0);
    lv_obj_set_style_text_font(home_batt_bolt, &font_ru_16, 0);
    lv_label_set_text(home_batt_bolt, "*");
    lv_obj_add_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);

    home_batt_lbl = lv_label_create(scr);
    lv_obj_set_pos(home_batt_lbl, 42, 42);
    lv_obj_set_style_text_color(home_batt_lbl, lv_color_hex(col.ink), 0);
    lv_label_set_text(home_batt_lbl, "--");

    lv_obj_t *coin = lv_img_create(scr);
    lv_img_set_src(coin, &img_coin);
    lv_img_set_zoom(coin, 150);
    lv_obj_set_pos(coin, 94, 38);
    lv_obj_clear_flag(coin, LV_OBJ_FLAG_CLICKABLE);
    home_coins = lv_label_create(scr);
    lv_obj_set_pos(home_coins, 116, 42);
    lv_obj_set_width(home_coins, 40);
    lv_label_set_long_mode(home_coins, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(home_coins, lv_color_hex(col.ink), 0);
    lv_label_set_text(home_coins, "0");

#if SHOW_FPS
    if (save_show_fps()) {
        home_fps = lv_label_create(scr);
        lv_obj_set_pos(home_fps, 160, 42);
        lv_obj_set_width(home_fps, 56);
        lv_label_set_long_mode(home_fps, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(home_fps, lv_color_hex(col.ink), 0);
        lv_label_set_text(home_fps, "FPS");
    }
#endif

    stage_open(scr, home_stage, 21, 62, 180, 140);
    home_mood = lv_label_create(scr);
    lv_obj_set_pos(home_mood, 16, 206);
    lv_obj_set_width(home_mood, 132);
    lv_label_set_long_mode(home_mood, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(home_mood, lv_color_hex(col.ink), 0);

    home_clock = lv_label_create(scr);
    lv_obj_set_pos(home_clock, 152, 206);
    lv_obj_set_width(home_clock, 62);
    lv_obj_set_style_text_align(home_clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(home_clock, lv_color_hex(col.ink), 0);
    lv_label_set_text(home_clock, "--:--");

    ui_label(scr, 16, 230, "Голод", &font_ru_16, col.ink);
    home_hunger = ui_bar(scr, 16, 250, 190, 12, col.gold);
    ui_label(scr, 16, 268, "Счастье", &font_ru_16, col.ink);
    home_happy = ui_bar(scr, 16, 288, 190, 12, col.hot);

    ui_button(scr, 6, 312, 104, 48, "Покормить", on_feed_click, NULL);
    ui_button(scr, 112, 312, 104, 48, "Играть", on_play_click, NULL);
    ui_button(scr, 6, 366, 104, 48, "Магазин", on_shop_click, NULL);
    ui_button(scr, 112, 366, 104, 48, "Статус", on_status_click, NULL);
    ui_button(scr, 6, 420, 210, 48, "Фото", on_camera_click, NULL);

    home_scr = scr;
    lv_obj_add_event_cb(scr, on_home_del, LV_EVENT_DELETE, home_stage.buf);
    if (home_stage.buf) stage_draw(home_stage, pet_view());
    return scr;
}

void screen_home_tick(uint32_t dt)
{
    (void)dt;
    if (!home_stage.live) return;
    uint8_t id = pet_active();
    PetView view = pet_view();
    stage_draw(home_stage, view);
    if (home_mood) {
        const char *mood = (pet_stat(id, STAT_HEALTH) < 20 && view.anim != ANIM_SLEEP) ? "Болеет" : pet_mood_name(view.anim);
        ui_set_text(home_mood, mood);
    }
    if (home_clock) {
        char cbuf[8];
        net_format_clock(cbuf, sizeof(cbuf), false);
        ui_set_text(home_clock, cbuf);
    }
    if (home_coins) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)pet_coins());
        ui_set_text(home_coins, buf);
    }
    if (home_hunger) lv_bar_set_value(home_hunger, pet_stat(id, STAT_HUNGER), LV_ANIM_OFF);
    if (home_happy) lv_bar_set_value(home_happy, pet_stat(id, STAT_HAPPY), LV_ANIM_OFF);
    if (home_batt_lbl) {
        char buf[16];
        if (power_battery_valid()) {
            int pct = power_battery_percent();
            snprintf(buf, sizeof(buf), "%d%%", pct);
            if (home_batt_fill) {
                int w = 1 + (pct * 22) / 100;
                if (w < 1) w = 1;
                if (w > 22) w = 22;
                lv_obj_set_width(home_batt_fill, w);
                uint32_t fill = col.mint;
                if (pct <= 15) fill = 0xD32F2F;
                else if (pct <= 30) fill = col.gold;
                if (power_charging()) fill = col.blue;
                lv_color_t want = lv_color_hex(fill);
                if (lv_obj_get_style_bg_color(home_batt_fill, 0).full != want.full) {
                    lv_obj_set_style_bg_color(home_batt_fill, want, 0);
                }
            }
        } else {
            snprintf(buf, sizeof(buf), "--");
            if (home_batt_fill) lv_obj_set_width(home_batt_fill, 1);
        }
        ui_set_text(home_batt_lbl, buf);
    }
    if (home_batt_bolt) {
        bool hidden = lv_obj_has_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);
        if (power_charge_done()) {
            ui_set_text(home_batt_bolt, "+");
            if (hidden) lv_obj_clear_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);
        } else if (power_charging()) {
            // Blink while charging so the plug state is obvious.
            ui_set_text(home_batt_bolt, "*");
            bool show = (millis() / 500) & 1;
            if (show && hidden) lv_obj_clear_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);
            else if (!show && !hidden) lv_obj_add_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);
        } else if (!hidden) {
            lv_obj_add_flag(home_batt_bolt, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if SHOW_FPS
    if (home_fps) {
        char buf[16];
        snprintf(buf, sizeof(buf), "FPS %u", display_fps());
        ui_set_text(home_fps, buf);
    }
#endif
}

static Stage pick_stage[2];
static lv_obj_t *pick_scr = nullptr;

struct PickOwn {
    uint16_t *a;
    uint16_t *b;
};

static void on_pick_del(lv_event_t *e)
{
    PickOwn *own = (PickOwn *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) != pick_scr) {
        if (own) {
            if (own->a && own->a != pick_stage[0].buf) ps_free(own->a);
            if (own->b && own->b != pick_stage[1].buf) ps_free(own->b);
            ps_free(own);
        }
        return;
    }
    stage_close(pick_stage[0]);
    stage_close(pick_stage[1]);
    pick_scr = nullptr;
    ps_free(own);
}

static void on_pick(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    pet_choose((uint8_t)id);
    audio_ok();
    ui_show(SCR_HOME);
}

lv_obj_t *screen_select_create()
{
    lv_obj_t *scr = ui_screen();
    if (pet_is_started()) ui_back_bar(scr, "Кто гуляет?");
    else ui_label(scr, 16, 14, "Кто гуляет?", &font_ru_22, col.ink);

    for (int i = 0; i < 2; i++) {
        int y = 64 + i * 200;
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_pos(card, 16, y);
        lv_obj_set_size(card, 190, 184);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(col.card), 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(col.line), 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        stage_open(card, pick_stage[i], 36, 4, 118, 120);
        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, pet_name((uint8_t)i));
        lv_obj_set_style_text_font(name, &font_ru_22, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(col.ink), 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    PickOwn *own = (PickOwn *)ps_alloc(sizeof(PickOwn));
    if (own) {
        own->a = pick_stage[0].buf;
        own->b = pick_stage[1].buf;
    }
    pick_scr = scr;
    lv_obj_add_event_cb(scr, on_pick_del, LV_EVENT_DELETE, own);
    for (int i = 0; i < 2; i++) stage_draw(pick_stage[i], pet_slot_view((uint8_t)i));
    return scr;
}

void screen_select_tick(uint32_t dt)
{
    (void)dt;
    if (!pick_stage[0].live && !pick_stage[1].live) return;
    for (int i = 0; i < 2; i++) stage_draw(pick_stage[i], pet_slot_view((uint8_t)i));
}

static lv_obj_t *stat_bar[5];
static lv_obj_t *stat_lbl[5];
static lv_obj_t *stat_info;
static lv_obj_t *btn_sleep = nullptr;
static bool stat_live = false;
static lv_obj_t *stat_scr = nullptr;

static void paint_sleep_btn()
{
    if (!btn_sleep) return;
    lv_obj_t *lbl = lv_obj_get_child(btn_sleep, 0);
    if (lbl) lv_label_set_text(lbl, pet_is_sleeping() ? "Будить" : "Спать");
}

static void on_stat_del(lv_event_t *e)
{
    if (lv_event_get_target(e) != stat_scr) return;
    stat_live = false;
    stat_scr = nullptr;
    stat_info = nullptr;
    btn_sleep = nullptr;
    for (int i = 0; i < 5; i++) stat_bar[i] = stat_lbl[i] = nullptr;
}

static void add_stat_row(lv_obj_t *scr, int index, int y, const char *name, uint32_t color)
{
    stat_lbl[index] = lv_label_create(scr);
    lv_obj_set_pos(stat_lbl[index], 16, y);
    lv_obj_set_style_text_color(stat_lbl[index], lv_color_hex(col.ink), 0);
    lv_label_set_text(stat_lbl[index], name);
    stat_bar[index] = ui_bar(scr, 16, y + 20, 190, 12, color);
}

static void on_clean(lv_event_t *e)
{
    (void)e;
    if (!pet_clean()) {
        audio_fail();
        ui_toast("Мало сил");
        return;
    }
    audio_ok();
    ui_toast("Чистая и довольная");
}

static void on_sleep(lv_event_t *e)
{
    (void)e;
    pet_toggle_sleep();
    paint_sleep_btn();
    ui_toast(pet_is_sleeping() ? "Сладких снов" : "Доброе утро");
}

static void on_switch_pet(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_SELECT);
}

static void do_reset()
{
    save_reset();
    ui_toast("Начнём сначала");
    ui_show(SCR_SELECT);
}

static void on_reset(lv_event_t *e)
{
    (void)e;
    ui_confirm("Сбросить питомца?", do_reset);
}

static void paint_stats()
{
    if (!stat_live) return;
    uint8_t id = pet_active();
    const int which[5] = {STAT_HUNGER, STAT_HAPPY, STAT_HEALTH, STAT_ENERGY, STAT_CLEAN};
    const char *names[5] = {"Голод", "Счастье", "Здоровье", "Энергия", "Чистота"};
    for (int i = 0; i < 5; i++) {
        int v = pet_stat(id, which[i]);
        if (stat_bar[i]) lv_bar_set_value(stat_bar[i], v, LV_ANIM_OFF);
        if (stat_lbl[i]) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s  %d", names[i], v);
            ui_set_text(stat_lbl[i], buf);
        }
    }
    if (stat_info) {
        char age[24];
        uint32_t sec = pet_age(id);
        uint32_t m = sec / 60;
        uint32_t h = m / 60;
        if (h > 0) snprintf(age, sizeof(age), "%lu ч %lu мин", (unsigned long)h, (unsigned long)(m % 60));
        else snprintf(age, sizeof(age), "%lu мин", (unsigned long)m);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s\nУр. %u   опыт %lu\nВозраст %s\nМонеты %lu",
                 pet_name(id), pet_level(id), (unsigned long)pet_xp(id), age, (unsigned long)pet_coins());
        ui_set_text(stat_info, buf);
    }
}

lv_obj_t *screen_status_create()
{
    lv_obj_t *scr = ui_screen();
    stat_live = true;
    stat_scr = scr;
    lv_obj_add_event_cb(scr, on_stat_del, LV_EVENT_DELETE, NULL);
    ui_back_bar(scr, "Статус");
    add_stat_row(scr, 0, 52, "Голод", col.gold);
    add_stat_row(scr, 1, 96, "Счастье", col.hot);
    add_stat_row(scr, 2, 140, "Здоровье", col.mint);
    add_stat_row(scr, 3, 184, "Энергия", col.blue);
    add_stat_row(scr, 4, 228, "Чистота", col.lilac);
    stat_info = lv_label_create(scr);
    lv_obj_set_pos(stat_info, 16, 272);
    lv_obj_set_style_text_color(stat_info, lv_color_hex(col.ink), 0);
    paint_stats();
    ui_button(scr, 8, 368, 100, 48, "Помыть", on_clean, NULL);
    btn_sleep = ui_button(scr, 114, 368, 100, 48, pet_is_sleeping() ? "Будить" : "Спать", on_sleep, NULL);
    ui_button(scr, 8, 424, 100, 48, "Сменить", on_switch_pet, NULL);
    ui_button(scr, 114, 424, 100, 48, "Сброс", on_reset, NULL);
    return scr;
}

void screen_status_tick(uint32_t dt)
{
    (void)dt;
    paint_stats();
}

static Stage feed_stage;
static lv_obj_t *feed_scr = nullptr;
static lv_obj_t *feed_list = nullptr;
static bool feed_dirty = false;

static void on_feed_del(lv_event_t *e)
{
    uint16_t *buf = (uint16_t *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) != feed_scr) {
        if (buf && buf != feed_stage.buf) ps_free(buf);
        return;
    }
    stage_close(feed_stage);
    feed_scr = nullptr;
    feed_list = nullptr;
    feed_dirty = false;
}

static void rebuild_feed(lv_obj_t *scr);

static void on_eat(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id == FOOD_MEDICINE && pet_stat(pet_active(), STAT_HEALTH) >= 95) {
        ui_toast("Уже здоров");
        return;
    }
    if (!pet_feed((uint8_t)id)) {
        audio_fail();
        ui_toast("Нет в корзинке");
        return;
    }
    audio_ok();
    ui_toast(id == FOOD_MEDICINE ? "Уже лучше" : "Ням-ням");
    feed_dirty = true;
}

static void on_goto_shop(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_SHOP);
}

static void rebuild_feed(lv_obj_t *scr)
{
    (void)scr;
    if (!feed_list) return;
    bool any = false;
    for (int i = 0; i < food_kind_count(); i++) {
        uint8_t n = pet_food_count((uint8_t)i);
        if (!n) continue;
        any = true;
        const FoodInfo *f = food_info(i);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s  x%u", f->name, n);
        ui_button(feed_list, 0, 0, 190, 48, buf, on_eat, (void *)(intptr_t)i);
    }
    if (!any) {
        ui_label(feed_list, 8, 8, "Корзинка пуста", &font_ru_16, col.ink);
        ui_button(feed_list, 8, 40, 170, 48, "В магазин", on_goto_shop, NULL);
    }
}

lv_obj_t *screen_feed_create()
{
    lv_obj_t *scr = ui_screen();
    ui_back_bar(scr, "Корм");
    stage_open(scr, feed_stage, 36, 50, 150, 140);
    feed_list = lv_obj_create(scr);
    lv_obj_set_pos(feed_list, 8, 200);
    lv_obj_set_size(feed_list, 206, 270);
    lv_obj_set_style_bg_color(feed_list, lv_color_hex(col.bg), 0);
    lv_obj_set_style_bg_opa(feed_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(feed_list, 0, 0);
    lv_obj_set_style_pad_all(feed_list, 0, 0);
    lv_obj_set_flex_flow(feed_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(feed_list, 8, 0);
    rebuild_feed(scr);
    feed_scr = scr;
    lv_obj_add_event_cb(scr, on_feed_del, LV_EVENT_DELETE, feed_stage.buf);
    stage_draw(feed_stage, pet_view());
    return scr;
}

void screen_feed_tick(uint32_t dt)
{
    (void)dt;
    if (!feed_stage.live) return;
    if (feed_dirty && feed_list) {
        feed_dirty = false;
        lv_obj_clean(feed_list);
        rebuild_feed(nullptr);
    }
    stage_draw(feed_stage, pet_view());
}
