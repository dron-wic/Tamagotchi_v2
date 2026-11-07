#pragma once

#include <stdint.h>

enum ScreenId : uint8_t {
    SCR_SELECT = 0,
    SCR_HOME,
    SCR_STATUS,
    SCR_SHOP,
    SCR_FEED,
    SCR_GAMES,
    SCR_GAME,
    SCR_SETTINGS,
    SCR_CAMERA,
    SCR_ALBUM,
    SCR_CAMSET,
};

void ui_init();
void ui_tick(uint32_t dt);
void ui_show(ScreenId id);
void ui_back();
void ui_go_home();
void ui_toast(const char *text);
void ui_play(int game_id);
void ui_confirm(const char *text, void (*on_yes)());
bool ui_has_modal();
void ui_restyle_chrome();
