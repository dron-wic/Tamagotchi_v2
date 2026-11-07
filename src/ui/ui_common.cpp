#include "ui/ui_common.h"

#include <string.h>

#include "board_config.h"
#include "hw/audio.h"
#include "ui/ui.h"
#include "util.h"

// Palettes follow color Tamagotchi contrast (Uni / Pix): near-white
// or near-black page, solid black/white body text, no pink fills.
// Selected chips use blue; pressed state is neutral gray.

static const UiColors LIGHT = {
    0xF5F5F8, // bg
    0x101018, // ink
    0x4A4A5A, // muted
    0x101018, // accent = ink
    0x2F6BFF, // hot fill (blue)
    0xFFFFFF, // on_hot
    0xFFFFFF, // card
    0xD8D8E2, // pressed
    0x101018, // line
    0xE0E0E8, // track
    0x00A36A, // mint
    0xD48A00, // gold
    0x2F6BFF, // blue
    0x7A4DFF, // lilac
    0xFFFFFF, // toast bg
    0x101018, // toast ink = ink
    false,
};

static const UiColors DARK = {
    0x0B0B16, // bg
    0xFFFFFF, // ink
    0xB8B8C8, // muted
    0xFFFFFF, // accent = ink
    0x5BA0FF, // hot fill (blue)
    0x0B0B16, // on_hot
    0x1A1A2C, // card
    0x2C2C44, // pressed
    0xFFFFFF, // line
    0x2A2A3A, // track
    0x39F0B0, // mint
    0xFFD24A, // gold
    0x5BA0FF, // blue
    0xC9A0FF, // lilac
    0x1A1A2C, // toast bg
    0xFFFFFF, // toast ink = ink
    true,
};

UiColors col = LIGHT;
static uint8_t mode_now = 0;

static lv_point_t press_pt;
static bool press_ok = false;

static void on_press(lv_event_t *e)
{
    (void)e;
    press_ok = false;
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_indev_get_point(in, &press_pt);
    press_ok = true;
}

// Registered before the real click handler. A finger that moved was a scroll.
static void on_click_guard(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_get_act();
    if (!in || !press_ok) return;
    lv_point_t now;
    lv_indev_get_point(in, &now);
    if (LV_ABS(now.x - press_pt.x) >= 10 || LV_ABS(now.y - press_pt.y) >= 10)
        lv_event_stop_processing(e);
}

void ui_drag_guard(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(obj, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, on_click_guard, LV_EVENT_CLICKED, NULL);
}

static void on_sound(lv_event_t *e)
{
    (void)e;
    audio_click();
}

static void on_title_back(lv_event_t *e)
{
    (void)e;
    ui_back();
}

void theme_apply(uint8_t mode)
{
    mode_now = mode & 1;
    col = mode_now ? DARK : LIGHT;
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) return;
    static uint8_t applied = 0xFF;
    if (applied == mode_now) return;
    applied = mode_now;
    lv_theme_t *theme = lv_theme_default_init(disp, lv_color_hex(col.hot), lv_color_hex(col.blue), col.dark, &font_ru_16);
    lv_disp_set_theme(disp, theme);
    lv_disp_set_bg_color(disp, lv_color_hex(col.bg));
    lv_disp_set_bg_opa(disp, LV_OPA_COVER);
}

uint8_t theme_mode()
{
    return mode_now;
}

uint16_t ui_bg565()
{
    return rgb565_hex(col.bg);
}

lv_obj_t *ui_screen()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(col.bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_shadow_width(scr, 0, 0);
    lv_obj_set_style_text_font(scr, &font_ru_16, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(col.ink), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    return scr;
}

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(col.card), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(col.pressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(col.line), 0);
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_ru_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    lv_obj_set_width(label, w - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label);
    ui_drag_guard(btn);
    lv_obj_add_event_cb(btn, on_sound, LV_EVENT_CLICKED, NULL);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    return btn;
}

lv_obj_t *ui_bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(col.track), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(col.line), LV_PART_MAIN);
    return bar;
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
}

void ui_label(lv_obj_t *parent, int x, int y, const char *text, const lv_font_t *font, uint32_t color)
{
    (void)color;
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
}

lv_obj_t *ui_back_bar(lv_obj_t *scr, const char *title)
{
    ui_button(scr, 8, 8, 64, 36, "Назад", on_title_back, NULL);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, title);
    // The slot beside "Назад" is 136 px. A title that does not fit at 22
    // drops to 16, otherwise the last letters are cut off.
    const lv_font_t *font = &font_ru_22;
    if (lv_txt_get_width(title, strlen(title), font, 0, LV_TEXT_FLAG_NONE) > 136)
        font = &font_ru_16;
    lv_obj_set_style_text_font(label, font, 0);
    lv_coord_t lh = lv_font_get_line_height(font);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    lv_obj_set_pos(label, 78, 8 + (36 - lh) / 2);
    lv_obj_set_width(label, 136);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}
