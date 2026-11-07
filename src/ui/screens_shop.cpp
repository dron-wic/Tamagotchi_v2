#include "ui/screens.h"

#include "assets/icons.h"
#include "hw/audio.h"
#include "pet/pet.h"
#include "pet/render.h"
#include "ui/font_ru.h"
#include "ui/ui.h"
#include "ui/ui_common.h"
#include "util.h"

#include <Arduino.h>
#include <stdio.h>

static bool live = false;
static bool dirty = false;
static int tab = 0;
static int selected = 0;
struct StagePreview {
    bool live;
    lv_obj_t *canvas;
    uint16_t *buf;
    int w;
    int h;
    int seen;
};

static StagePreview preview;

static lv_obj_t *list = nullptr;
static lv_obj_t *blurb = nullptr;
static lv_obj_t *action = nullptr;

static const lv_img_dsc_t *FOOD_ICON[FOOD_COUNT] = {
    &img_cookie, &img_cake, &img_apple, &img_milk, &img_candy, &img_heart,
};

static void close_preview()
{
    preview.live = false;
    preview.canvas = nullptr;
    if (preview.buf) ps_free(preview.buf);
    preview.buf = nullptr;
}

static lv_obj_t *shop_scr = nullptr;

static void on_shop_del(lv_event_t *e)
{
    uint16_t *buf = (uint16_t *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) != shop_scr) {
        if (buf && buf != preview.buf) ps_free(buf);
        return;
    }
    live = false;
    dirty = false;
    shop_scr = nullptr;
    list = blurb = action = nullptr;
    close_preview();
}

static PetView preview_view()
{
    PetView v = pet_view();
    if (tab == 1) {
        const ClothInfo *c = cloth_info(selected);
        if (c->slot == SLOT_HEAD) v.hat = c->id;
        else if (c->slot == SLOT_BODY) v.outfit = c->id;
        else v.extra = c->id;
    }
    v.bg = ui_bg565();
    return v;
}

static void draw_preview()
{
    if (!preview.live || !preview.buf) return;
    PetView v = preview_view();
    if (v.frame == preview.seen && !dirty) return;
    preview.seen = v.frame;
    pet_render(preview.buf, preview.w, preview.h, v);
    if (preview.canvas) lv_obj_invalidate(preview.canvas);
}

static void refresh_text()
{
    if (!blurb || !action) return;
    char price[96];
    const char *button = "Купить";
    const char *about = "";
    if (tab == 0) {
        const FoodInfo *f = food_info(selected);
        about = f->blurb;
        snprintf(price, sizeof(price), "%s\n%u монет\nв корзинке %u", f->name, f->price, pet_food_count((uint8_t)selected));
        if (pet_food_count((uint8_t)selected) >= 9) button = "Полно";
    } else {
        const ClothInfo *c = cloth_info(selected);
        about = c->blurb;
        bool own = pet_owns(c->id);
        uint8_t eq = pet_equipped(pet_active(), c->slot);
        snprintf(price, sizeof(price), "%s\n%u монет%s", c->name, c->price, own ? "\nесть" : "");
        if (!own) button = "Купить";
        else button = (eq == c->id) ? "Снять" : "Надеть";
    }
    lv_label_set_text(blurb, price);
    (void)about;
    lv_obj_t *lbl = lv_obj_get_child(action, 0);
    if (lbl) lv_label_set_text(lbl, button);
}

static void paint_row(lv_obj_t *row, int index)
{
    char buf[48];
    if (tab == 0) {
        const FoodInfo *f = food_info(index);
        snprintf(buf, sizeof(buf), "%s%s  %u", index == selected ? "> " : "", f->name, f->price);
    } else {
        const ClothInfo *c = cloth_info(index);
        snprintf(buf, sizeof(buf), "%s%s  %u", index == selected ? "> " : "", c->name, c->price);
    }
    lv_obj_set_style_bg_color(row, lv_color_hex(index == selected ? col.pressed : col.card), 0);
    // Last child is always the label (icon, if any, comes first).
    uint32_t n = lv_obj_get_child_cnt(row);
    if (n == 0) return;
    lv_obj_t *lbl = lv_obj_get_child(row, n - 1);
    if (lbl) lv_label_set_text(lbl, buf);
}

static void on_row_click(lv_event_t *e)
{
    int next = (int)(intptr_t)lv_event_get_user_data(e);
    if (next == selected) return;
    int prev = selected;
    selected = next;
    // Update only the two affected rows so the list scroll stays put.
    if (list) {
        uint32_t n = lv_obj_get_child_cnt(list);
        if ((uint32_t)prev < n) paint_row(lv_obj_get_child(list, prev), prev);
        if ((uint32_t)selected < n) paint_row(lv_obj_get_child(list, selected), selected);
    }
    preview.seen = -1;
    refresh_text();
}

static void rebuild_list()
{
    if (!list) return;
    lv_coord_t scroll_y = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    int count = tab == 0 ? food_kind_count() : cloth_kind_count();
    for (int i = 0; i < count; i++) {
        const lv_img_dsc_t *icon = (tab == 0) ? FOOD_ICON[i] : nullptr;
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, 190, 48);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(col.line), 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        ui_drag_guard(row);
        lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (icon) {
            lv_obj_t *img = lv_img_create(row);
            lv_img_set_src(img, icon);
            lv_obj_align(img, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, lv_color_hex(col.ink), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, icon ? 36 : 6, 0);
        paint_row(row, i);
    }
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll_y, LV_ANIM_OFF);
}

static void on_tab(lv_event_t *e)
{
    int next = (int)(intptr_t)lv_event_get_user_data(e);
    if (next == tab) return;
    tab = next;
    selected = 0;
    preview.seen = -1;
    dirty = true;
}

static void on_action(lv_event_t *e)
{
    (void)e;
    if (tab == 0) {
        if (pet_food_count((uint8_t)selected) >= 9) {
            ui_toast("Корзинка полная");
            return;
        }
        if (!pet_buy_food((uint8_t)selected)) {
            audio_fail();
            ui_toast("Мало монет");
            return;
        }
        audio_coin();
        ui_toast("В корзинке");
    } else {
        const ClothInfo *c = cloth_info(selected);
        if (pet_owns(c->id)) {
            pet_toggle_cloth(c->id);
            audio_click();
        } else if (!pet_buy_cloth(c->id)) {
            audio_fail();
            ui_toast("Мало монет");
            return;
        } else {
            audio_coin();
            ui_toast("Куплено");
        }
        preview.seen = -1;
        dirty = true; // clothes list may show ownership later — rebuild ok
        return;
    }
    refresh_text();
}

lv_obj_t *screen_shop_create()
{
    lv_obj_t *scr = ui_screen();
    live = true;
    dirty = true;
    selected = 0;
    ui_back_bar(scr, "Магазин");
    ui_button(scr, 8, 50, 100, 36, "Еда", on_tab, (void *)(intptr_t)0);
    ui_button(scr, 114, 50, 100, 36, "Одежда", on_tab, (void *)(intptr_t)1);

    preview.live = true;
    preview.w = 140;
    preview.h = 120;
    preview.seen = -1;
    preview.buf = (uint16_t *)ps_alloc((size_t)preview.w * preview.h * 2);
    preview.canvas = lv_canvas_create(scr);
    lv_obj_set_pos(preview.canvas, 41, 92);
    lv_obj_clear_flag(preview.canvas, LV_OBJ_FLAG_CLICKABLE);
    if (preview.buf) {
        uint16_t bg = ui_bg565();
        int n = preview.w * preview.h;
        for (int i = 0; i < n; i++) preview.buf[i] = bg;
        lv_canvas_set_buffer(preview.canvas, preview.buf, preview.w, preview.h, LV_IMG_CF_TRUE_COLOR);
    }

    blurb = lv_label_create(scr);
    lv_obj_set_pos(blurb, 12, 214);
    lv_obj_set_size(blurb, 200, 66);
    lv_label_set_long_mode(blurb, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(blurb, lv_color_hex(col.ink), 0);

    action = ui_button(scr, 56, 286, 110, 40, "Купить", on_action, NULL);

    list = lv_obj_create(scr);
    lv_obj_set_pos(list, 8, 332);
    lv_obj_set_size(list, 206, 142);
    lv_obj_set_style_bg_color(list, lv_color_hex(col.bg), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    shop_scr = scr;
    lv_obj_add_event_cb(scr, on_shop_del, LV_EVENT_DELETE, preview.buf);
    draw_preview();
    return scr;
}

void screen_shop_tick(uint32_t dt)
{
    (void)dt;
    if (!live) return;
    if (dirty) {
        dirty = false;
        rebuild_list();
        refresh_text();
        preview.seen = -1;
    }
    draw_preview();
}
