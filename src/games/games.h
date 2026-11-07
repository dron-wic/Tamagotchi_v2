#pragma once

#include "lvgl.h"

enum GameId : int {
    GAME_TAP = 0,
    GAME_REACT,
    GAME_MEMORY,
    GAME_CHASE,
};

lv_obj_t *games_menu_create();
lv_obj_t *game_create(int id);
void game_tick(uint32_t dt);
bool game_on_back();
