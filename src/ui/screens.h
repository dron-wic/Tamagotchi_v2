#pragma once

#include "lvgl.h"

lv_obj_t *screen_select_create();
void screen_select_tick(uint32_t dt);
lv_obj_t *screen_home_create();
void screen_home_tick(uint32_t dt);
lv_obj_t *screen_status_create();
void screen_status_tick(uint32_t dt);
lv_obj_t *screen_feed_create();
void screen_feed_tick(uint32_t dt);
lv_obj_t *screen_shop_create();
void screen_shop_tick(uint32_t dt);
lv_obj_t *screen_settings_create();
void screen_settings_tick(uint32_t dt);
// True when Back leaves a settings section for the section list.
bool screen_settings_back();
lv_obj_t *screen_camera_create();
void screen_camera_tick(uint32_t dt);
lv_obj_t *screen_camset_create();
lv_obj_t *screen_album_create();
void screen_album_tick(uint32_t dt);
