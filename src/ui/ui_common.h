#pragma once

#include "lvgl.h"
#include "ui/font_ru.h"

// Runtime palette modeled on color Tamagotchi UI (Uni / Pix):
// black or white body text, blue selected fills, hard borders.
struct UiColors {
    uint32_t bg;
    uint32_t ink;
    uint32_t muted;
    uint32_t accent;
    uint32_t hot;
    uint32_t on_hot;
    uint32_t card;
    uint32_t pressed;
    uint32_t line;
    uint32_t track;
    uint32_t mint;
    uint32_t gold;
    uint32_t blue;
    uint32_t lilac;
    uint32_t toast_bg;
    uint32_t toast_ink;
    bool dark;
};

extern UiColors col;

void theme_apply(uint8_t mode);
uint8_t theme_mode();

uint16_t ui_bg565();
lv_obj_t *ui_screen();
lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_event_cb_t cb, void *user);
// A drag that starts on this object scrolls the parent and does not count as a click.
void ui_drag_guard(lv_obj_t *obj);
lv_obj_t *ui_bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
void ui_label(lv_obj_t *parent, int x, int y, const char *text, const lv_font_t *font, uint32_t color);
// Skips the update when the string is unchanged. lv_label_set_text always invalidates.
void ui_set_text(lv_obj_t *label, const char *text);
lv_obj_t *ui_back_bar(lv_obj_t *scr, const char *title);
