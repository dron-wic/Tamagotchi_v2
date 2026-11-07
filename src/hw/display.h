#pragma once

#include <stddef.h>
#include <stdint.h>

void display_init();
void display_task();
uint16_t display_fps();
// Direct RGB565 blit. Bypasses LVGL, so a later flush of the same rectangle
// paints over it. Prefer an lv_canvas for anything that stays on screen.
void display_blit_rgb565(int x, int y, int w, int h, const uint16_t *px);
// Decode JPEG into dst (RGB565), scaled to fit dw x dh. Uses LovyanGFX.
bool display_jpg_to_rgb565(const uint8_t *jpg, size_t len, uint16_t *dst, int dw, int dh);