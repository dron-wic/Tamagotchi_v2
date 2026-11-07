#include "ui/ui.h"

#include "board_config.h"
#include "games/games.h"
#include "hw/input.h"
#include "hw/power.h"
#include "hw/touch.h"
#include "pet/pet.h"
#include "storage/save.h"
#include "ui/font_ru.h"
#include "ui/prefs.h"
#include "ui/screens.h"
#include "ui/ui_common.h"

#include <Arduino.h>
#include <stdio.h>

static ScreenId current = SCR_HOME;
static int pending_game = 0;
static lv_obj_t *toast = nullptr;
static uint32_t toast_until = 0;
static lv_obj_t *modal = nullptr;
static void (*modal_yes)() = nullptr;
static bool want_yes = false;
static bool want_close = false;
static lv_obj_t *build(ScreenId id)
{
    switch (id) {
    case SCR_SELECT: return screen_select_create();
    case SCR_HOME: return screen_home_create();
    case SCR_STATUS: return screen_status_create();
    case SCR_SHOP: return screen_shop_create();
    case SCR_FEED: return screen_feed_create();
    case SCR_GAMES: return games_menu_create();
    case SCR_GAME: return game_create(pending_game);
    case SCR_SETTINGS: return screen_settings_create();
    case SCR_CAMERA: return screen_camera_create();
    case SCR_ALBUM: return screen_album_create();
    case SCR_CAMSET: return screen_camset_create();
    }
    return screen_home_create();
}

static void close_modal()
{
    if (modal) {
        lv_obj_del(modal);
        modal = nullptr;
    }
    modal_yes = nullptr;
}

static bool transition_busy()
{
    lv_disp_t *disp = lv_disp_get_default();
    return disp && (disp->scr_to_load != nullptr || disp->prev_scr != nullptr);
}

static void present(lv_obj_t *scr)
{
    // The new tree is already built. Put it on the panel before the old
    // screen is destroyed: deleting first left one refresh with nothing
    // to draw, and that frame is the black blink between menus.
    lv_obj_t *old = lv_scr_act();
    lv_scr_load(scr);
    lv_refr_now(NULL);
    if (old && old != scr) lv_obj_del(old);
}

void ui_show(ScreenId id)
{
    if (transition_busy()) return;
    current = id;
    present(build(id));
}

void ui_back()
{
    if (modal) {
        want_close = true;
        return;
    }
    if (transition_busy()) return;
    if (current == SCR_GAME && game_on_back()) return;
    if (current == SCR_HOME) return;
    if (current == SCR_SELECT && !pet_is_started()) return;
    if (current == SCR_GAME) ui_show(SCR_GAMES);
    else if (current == SCR_FEED) ui_show(SCR_HOME);
    else if (current == SCR_SETTINGS && screen_settings_back()) ui_show(SCR_SETTINGS);
    else if (current == SCR_CAMSET) ui_show(SCR_CAMERA);
    else ui_show(SCR_HOME);
}

void ui_go_home()
{
    if (modal) {
        want_close = true;
        return;
    }
    if (transition_busy()) return;
    if (!pet_is_started()) return;
    if (current == SCR_HOME) return;
    if (current == SCR_GAME) game_on_back();
    ui_show(SCR_HOME);
}

void ui_play(int game_id)
{
    if (pet_is_sleeping()) {
        ui_toast("Сначала разбуди");
        return;
    }
    pending_game = game_id;
    ui_show(SCR_GAME);
}

void ui_toast(const char *text)
{
    if (!toast) {
        toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(toast, lv_color_hex(col.card), 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(toast, lv_color_hex(col.ink), 0);
        lv_obj_set_style_text_font(toast, &font_ru_16, 0);
        lv_obj_set_style_border_width(toast, 3, 0);
        lv_obj_set_style_border_color(toast, lv_color_hex(col.line), 0);
        lv_obj_set_width(toast, LCD_W - 24);
        lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(toast, 12, 0);
        lv_obj_set_style_pad_ver(toast, 6, 0);
        lv_obj_set_style_radius(toast, 14, 0);
        lv_obj_clear_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_label_set_text(toast, text);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -14);
    toast_until = millis() + 1500;
}

static void on_modal_yes(lv_event_t *e)
{
    (void)e;
    want_yes = true;
}

static void on_modal_no(lv_event_t *e)
{
    (void)e;
    want_close = true;
}

void ui_confirm(const char *text, void (*on_yes)())
{
    close_modal();
    modal_yes = on_yes;
    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, LCD_W, LCD_H);
    // Near-opaque scrim so camera/album RGB blit does not tint the dialog green.
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *card = lv_obj_create(modal);
    lv_obj_set_size(card, 190, 160);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(col.card), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(col.line), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_text_font(card, &font_ru_16, 0);
    lv_obj_set_style_text_color(card, lv_color_hex(col.ink), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_ru_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 170);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 16);
    ui_button(card, 12, 100, 76, 44, "Да", on_modal_yes, NULL);
    ui_button(card, 100, 100, 76, 44, "Нет", on_modal_no, NULL);
}

bool ui_has_modal()
{
    return modal != nullptr;
}

void ui_restyle_chrome()
{
    if (!toast) return;
    lv_obj_set_style_bg_color(toast, lv_color_hex(col.card), 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(col.ink), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(col.line), 0);
}

void ui_init()
{
    prefs_apply();
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    if (!pet_is_started()) ui_show(SCR_SELECT);
    else {
        ui_show(SCR_HOME);
        if (pet_offline_steps() > 0) ui_toast("С возвращением");
    }
}

static void go_next_screen()
{
    if (current == SCR_HOME) ui_show(SCR_GAMES);
    else if (current == SCR_GAMES) ui_show(SCR_SHOP);
    else if (current == SCR_SHOP) ui_show(SCR_STATUS);
    else if (current == SCR_STATUS) ui_show(SCR_SETTINGS);
    else if (current == SCR_SETTINGS) ui_show(SCR_HOME);
}

static void run_key(uint8_t act)
{
    switch (act) {
    case BTN_ACT_BACK:
        ui_back();
        break;
    case BTN_ACT_NEXT:
        go_next_screen();
        break;
    case BTN_ACT_ACTION:
        if (current == SCR_HOME) ui_show(SCR_FEED);
        break;
    case BTN_ACT_FEED:
        ui_show(SCR_FEED);
        break;
    case BTN_ACT_GAMES:
        ui_show(SCR_GAMES);
        break;
    case BTN_ACT_SHOP:
        ui_show(SCR_SHOP);
        break;
    case BTN_ACT_STATUS:
        ui_show(SCR_STATUS);
        break;
    case BTN_ACT_SETTINGS:
        ui_show(SCR_SETTINGS);
        break;
    case BTN_ACT_HOME:
        ui_go_home();
        break;
    case BTN_ACT_SLEEP:
        if (pet_is_started()) pet_toggle_sleep();
        break;
    default:
        break;
    }
}

void ui_tick(uint32_t dt)
{
    if (want_yes) {
        want_yes = false;
        void (*fn)() = modal_yes;
        close_modal();
        if (fn) fn();
    } else if (want_close) {
        want_close = false;
        close_modal();
    }

    if (toast && (int32_t)(millis() - toast_until) > 0) {
        lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    }

    if (power_swallow_touch()) {
        touch_take_press();
        touch_take_home();
        input_take(BTN_BACK);
        input_take(BTN_OK);
        input_take(BTN_NEXT);
        return;
    }

    if (modal) {
        for (uint8_t i = 0; i < 3; i++) {
            if (!input_take((ButtonId)i)) continue;
            if (save_btn_act(i) == BTN_ACT_BACK) want_close = true;
        }
        touch_take_home();
        return;
    }

    if (touch_take_home()) ui_go_home();

    // Don't redraw canvases mid-slide — keeps the transition clean.
    if (!transition_busy()) {
        if (current == SCR_GAME) game_tick(dt);
        screen_select_tick(dt);
        screen_home_tick(dt);
        screen_status_tick(dt);
        screen_feed_tick(dt);
        screen_shop_tick(dt);
        screen_settings_tick(dt);
        screen_camera_tick(dt);
        screen_album_tick(dt);
    }

    if (current == SCR_GAME) {
        for (uint8_t i = 0; i < 3; i++) {
            if (!input_take((ButtonId)i)) continue;
            if (!transition_busy() && save_btn_act(i) == BTN_ACT_BACK) ui_back();
        }
        return;
    }
    if (transition_busy()) {
        input_take(BTN_BACK);
        input_take(BTN_OK);
        input_take(BTN_NEXT);
        return;
    }

    for (uint8_t i = 0; i < 3; i++) {
        if (!input_take((ButtonId)i)) continue;
        run_key(save_btn_act(i));
    }
}
