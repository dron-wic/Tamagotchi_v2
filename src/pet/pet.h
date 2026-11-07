#pragma once

#include <stdint.h>

enum PetAnim : uint8_t {
    ANIM_IDLE = 0,
    ANIM_HAPPY,
    ANIM_HUNGRY,
    ANIM_SLEEP,
    ANIM_EAT,
    ANIM_SAD,
};

enum ClothSlot : uint8_t {
    SLOT_HEAD = 0,
    SLOT_BODY = 1,
    SLOT_EXTRA = 2,
};

enum ClothId : uint8_t {
    CLOTH_NONE = 0,
    CLOTH_BOW = 1,
    CLOTH_HAT = 2,
    CLOTH_DRESS = 3,
    CLOTH_WINGS = 4,
    CLOTH_STAR = 5,
    CLOTH_CAPE = 6,
    CLOTH_COUNT = 7,
};

enum FoodId : uint8_t {
    FOOD_COOKIE = 0,
    FOOD_CAKE,
    FOOD_APPLE,
    FOOD_MILK,
    FOOD_CANDY,
    FOOD_MEDICINE,
    FOOD_COUNT,
};

struct FoodInfo {
    const char *name;
    const char *blurb;
    uint8_t price;
    int8_t hunger;
    int8_t happy;
    int8_t health;
    int8_t energy;
    int8_t clean;
};

struct ClothInfo {
    uint8_t id;
    uint8_t slot;
    const char *name;
    const char *blurb;
    uint8_t price;
    uint32_t swatch;
};

struct PetView {
    uint8_t species;
    uint8_t anim;
    uint8_t frame;
    uint8_t hat;
    uint8_t outfit;
    uint8_t extra;
    uint8_t bubbles;
    uint16_t bg;
};

static const int PET_SLOTS = 2;

void pet_init();
void pet_tick(uint32_t dt_ms);
bool pet_is_started();
uint8_t pet_active();
void pet_choose(uint8_t species);
const char *pet_name(uint8_t species);
const char *pet_mood_name(uint8_t anim);

PetView pet_view();
PetView pet_slot_view(uint8_t species);

int pet_stat(uint8_t species, int which);
uint32_t pet_coins();
uint32_t pet_age(uint8_t species);
uint16_t pet_level(uint8_t species);
uint32_t pet_xp(uint8_t species);
uint8_t pet_food_count(uint8_t id);
bool pet_owns(uint8_t cloth_id);
uint8_t pet_equipped(uint8_t species, uint8_t slot);

int food_kind_count();
const FoodInfo *food_info(int index);
int cloth_kind_count();
const ClothInfo *cloth_info(int index);

bool pet_feed(uint8_t food_id);
bool pet_buy_food(uint8_t food_id);
bool pet_buy_cloth(uint8_t cloth_id);
void pet_toggle_cloth(uint8_t cloth_id);
bool pet_clean();
void pet_toggle_sleep();
void pet_set_sleeping(bool on);
bool pet_is_sleeping();
bool pet_can_play();
bool pet_reward(int coins, int happy, int energy_cost, int xp_gain);
bool pet_spend_coins(uint32_t n);
bool pet_add_coins(uint32_t n);
uint32_t pet_offline_steps();

enum PetStat : int {
    STAT_HUNGER = 0,
    STAT_HAPPY,
    STAT_HEALTH,
    STAT_ENERGY,
    STAT_CLEAN,
};
