#include "pet/pet.h"

#include "board_config.h"
#include "storage/save.h"
#include "ui/ui_common.h"
#include "util.h"

#include <Arduino.h>

static const char *NAMES[2] = {"Мими", "Нори"};

static const FoodInfo FOODS[] = {
    {"Печенье", "Сладкое и сытное", 8, 24, 6, 0, 0, 0},
    {"Тортик", "Маленький праздник", 18, 34, 16, 0, -2, -4},
    {"Яблоко", "Свежее и полезное", 6, 16, 2, 8, 0, 0},
    {"Молоко", "Чтобы набраться сил", 10, 14, 4, 2, 16, 0},
    {"Конфета", "Быстрая радость", 12, 6, 22, 0, 2, -2},
    {"Лекарство", "Если болеет", 10, 0, 2, 36, 0, 0},
};

static const ClothInfo CLOTHES[] = {
    {CLOTH_BOW, SLOT_HEAD, "Бантик", "На ушко", 22, 0xFF4F93},
    {CLOTH_HAT, SLOT_HEAD, "Шляпка", "С ягодкой", 36, 0xFF8FB8},
    {CLOTH_DRESS, SLOT_BODY, "Платье", "Праздничное", 48, 0xFFD0E4},
    {CLOTH_WINGS, SLOT_EXTRA, "Крылья", "Совсем маленькие", 60, 0xF4F7FF},
    {CLOTH_STAR, SLOT_HEAD, "Звёздочка", "Заколка", 28, 0xFFD24A},
    {CLOTH_CAPE, SLOT_BODY, "Плащ", "Вечерний", 52, 0x3A3148},
};

static uint8_t frame = 0;
static uint32_t anim_acc = 0;
static uint32_t sec_acc = 0;
static uint16_t need_sub = 0;
static uint32_t eat_until = 0;
static uint32_t wash_until = 0;
static uint32_t cheer_until = 0;
static uint32_t offline_steps = 0;

static PetSlot &slot(uint8_t species)
{
    return save_data().pet[species & 1];
}

static PetSlot &active()
{
    return slot(save_data().active);
}

static void touch_level(PetSlot &p)
{
    uint32_t xp = p.xp;
    uint16_t level = 1;
    if (xp / 50 < 98) level = (uint16_t)(1 + xp / 50);
    else level = 99;
    p.level = level;
}

static void decay_one(PetSlot &p)
{
    if (p.sleeping) {
        p.energy = (int8_t)clampv<int>(p.energy + 3, 0, 100);
        p.hunger = (int8_t)clampv<int>(p.hunger - 1, 0, 100);
        p.happy = (int8_t)clampv<int>(p.happy - 1, 0, 100);
    } else {
        p.hunger = (int8_t)clampv<int>(p.hunger - 2, 0, 100);
        p.happy = (int8_t)clampv<int>(p.happy - 1, 0, 100);
        p.energy = (int8_t)clampv<int>(p.energy - 1, 0, 100);
        p.clean = (int8_t)clampv<int>(p.clean - 1, 0, 100);
    }
    if (p.hunger < 15 || p.clean < 15) {
        if (p.health > 0) p.health--;
    } else if (p.health < 100 && p.hunger > 55 && p.clean > 55 && p.happy > 40) {
        p.health++;
    }
    if (p.hunger < 12 && p.happy > 0) p.happy--;
}

static uint8_t anim_for(const PetSlot &p, bool is_active)
{
    if (p.sleeping) return ANIM_SLEEP;
    if (is_active) {
        uint32_t now = millis();
        if (now < eat_until) return ANIM_EAT;
        if (now < cheer_until) return ANIM_HAPPY;
    }
    if (p.health < 22 || p.happy < 22) return ANIM_SAD;
    if (p.hunger < 28) return ANIM_HUNGRY;
    if (p.happy > 78) return ANIM_HAPPY;
    return ANIM_IDLE;
}

static PetView make_view(uint8_t species)
{
    PetSlot &p = slot(species);
    bool is_active = species == (save_data().active & 1);
    PetView v = {};
    v.species = species & 1;
    v.anim = anim_for(p, is_active);
    v.frame = frame;
    v.hat = p.head;
    v.outfit = p.outfit;
    v.extra = p.extra;
    v.bubbles = is_active && millis() < wash_until ? 1 : 0;
    v.bg = ui_bg565();
    return v;
}

void pet_init()
{
    SaveData &d = save_data();
    uint32_t now = save_clock_now();
    if (d.started && now && d.last_unix && now > d.last_unix) {
        uint32_t dt = now - d.last_unix;
        if (dt > 8u * 3600u) dt = 8u * 3600u;
        uint32_t steps = dt / save_pace_sec();
        PetSlot &p = active();
        for (uint32_t i = 0; i < steps; i++) decay_one(p);
        p.age_sec += dt;
        offline_steps = steps;
        save_mark();
        Serial.printf("Offline steps %lu\n", (unsigned long)steps);
    }
}

void pet_tick(uint32_t dt)
{
    anim_acc += dt;
    while (anim_acc >= 33) {
        anim_acc -= 33;
        frame++;
    }
    if (!save_data().started) return;
    sec_acc += dt;
    while (sec_acc >= 1000) {
        sec_acc -= 1000;
        PetSlot &p = active();
        p.age_sec++;
        need_sub++;
        if (need_sub >= save_pace_sec()) {
            need_sub = 0;
            decay_one(p);
            save_mark();
        }
    }
}

bool pet_is_started()
{
    return save_data().started != 0;
}

uint8_t pet_active()
{
    return save_data().active & 1;
}

void pet_choose(uint8_t species)
{
    save_data().active = species & 1;
    save_data().started = 1;
    eat_until = wash_until = cheer_until = 0;
    save_flush();
}

const char *pet_name(uint8_t species)
{
    return NAMES[species & 1];
}

const char *pet_mood_name(uint8_t anim)
{
    switch (anim) {
    case ANIM_HAPPY: return "Радуется";
    case ANIM_HUNGRY: return "Проголодалась";
    case ANIM_SLEEP: return "Спит";
    case ANIM_EAT: return "Ням-ням";
    case ANIM_SAD: return "Грустит";
    default: return "Мурчит";
    }
}

PetView pet_view()
{
    return make_view(save_data().active & 1);
}

PetView pet_slot_view(uint8_t species)
{
    return make_view(species & 1);
}

int pet_stat(uint8_t species, int which)
{
    PetSlot &p = slot(species);
    switch (which) {
    case STAT_HUNGER: return p.hunger;
    case STAT_HAPPY: return p.happy;
    case STAT_HEALTH: return p.health;
    case STAT_ENERGY: return p.energy;
    case STAT_CLEAN: return p.clean;
    default: return 0;
    }
}

uint32_t pet_coins() { return save_data().coins; }
uint32_t pet_age(uint8_t species) { return slot(species).age_sec; }
uint16_t pet_level(uint8_t species) { return slot(species).level; }
uint32_t pet_xp(uint8_t species) { return slot(species).xp; }

uint8_t pet_food_count(uint8_t id)
{
    if (id >= 8) return 0;
    return save_data().food[id];
}

bool pet_owns(uint8_t cloth_id)
{
    if (cloth_id == 0 || cloth_id >= 32) return false;
    return (save_data().clothes & (1u << cloth_id)) != 0;
}

uint8_t pet_equipped(uint8_t species, uint8_t which_slot)
{
    PetSlot &p = slot(species);
    if (which_slot == SLOT_HEAD) return p.head;
    if (which_slot == SLOT_BODY) return p.outfit;
    return p.extra;
}

int food_kind_count() { return FOOD_COUNT; }
const FoodInfo *food_info(int index)
{
    if (index < 0 || index >= FOOD_COUNT) return &FOODS[0];
    return &FOODS[index];
}
int cloth_kind_count() { return (int)(sizeof(CLOTHES) / sizeof(CLOTHES[0])); }
const ClothInfo *cloth_info(int index)
{
    if (index < 0 || index >= cloth_kind_count()) return &CLOTHES[0];
    return &CLOTHES[index];
}

bool pet_feed(uint8_t food_id)
{
    if (food_id >= FOOD_COUNT) return false;
    if (save_data().food[food_id] == 0) return false;
    save_data().food[food_id]--;
    const FoodInfo &f = FOODS[food_id];
    PetSlot &p = active();
    if (food_id == FOOD_MEDICINE && p.health >= 95) {
        save_data().food[food_id]++;
        return false;
    }
    p.sleeping = 0;
    p.hunger = (int8_t)clampv<int>(p.hunger + f.hunger, 0, 100);
    p.happy = (int8_t)clampv<int>(p.happy + f.happy, 0, 100);
    p.health = (int8_t)clampv<int>(p.health + f.health, 0, 100);
    p.energy = (int8_t)clampv<int>(p.energy + f.energy, 0, 100);
    p.clean = (int8_t)clampv<int>(p.clean + f.clean, 0, 100);
    p.xp += 2;
    touch_level(p);
    eat_until = millis() + 1700;
    cheer_until = eat_until;
    save_flush();
    return true;
}

bool pet_buy_food(uint8_t food_id)
{
    if (food_id >= FOOD_COUNT) return false;
    const FoodInfo &f = FOODS[food_id];
    if (save_data().coins < f.price) return false;
    if (save_data().food[food_id] >= 9) return false;
    save_data().coins -= f.price;
    save_data().food[food_id]++;
    save_flush();
    return true;
}

bool pet_buy_cloth(uint8_t cloth_id)
{
    const ClothInfo *info = nullptr;
    for (int i = 0; i < cloth_kind_count(); i++) {
        if (CLOTHES[i].id == cloth_id) info = &CLOTHES[i];
    }
    if (!info) return false;
    if (pet_owns(cloth_id)) return true;
    if (save_data().coins < info->price) return false;
    save_data().coins -= info->price;
    save_data().clothes |= (1u << cloth_id);
    pet_toggle_cloth(cloth_id);
    save_flush();
    return true;
}

void pet_toggle_cloth(uint8_t cloth_id)
{
    const ClothInfo *info = nullptr;
    for (int i = 0; i < cloth_kind_count(); i++) {
        if (CLOTHES[i].id == cloth_id) info = &CLOTHES[i];
    }
    if (!info || !pet_owns(cloth_id)) return;
    PetSlot &p = active();
    uint8_t *field = &p.head;
    if (info->slot == SLOT_BODY) field = &p.outfit;
    else if (info->slot == SLOT_EXTRA) field = &p.extra;
    *field = (*field == cloth_id) ? 0 : cloth_id;
    save_flush();
}

bool pet_clean()
{
    PetSlot &p = active();
    if (p.energy < 8) return false;
    p.sleeping = 0;
    p.clean = 100;
    p.happy = (int8_t)clampv<int>(p.happy + 8, 0, 100);
    p.energy = (int8_t)clampv<int>(p.energy - 6, 0, 100);
    wash_until = millis() + 1800;
    cheer_until = wash_until;
    save_flush();
    return true;
}

void pet_toggle_sleep()
{
    pet_set_sleeping(!pet_is_sleeping());
}

void pet_set_sleeping(bool on)
{
    PetSlot &p = active();
    uint8_t v = on ? 1 : 0;
    if (p.sleeping == v) return;
    p.sleeping = v;
    save_flush();
}

bool pet_is_sleeping()
{
    return active().sleeping != 0;
}

bool pet_can_play()
{
    return !active().sleeping;
}

bool pet_reward(int coins, int happy, int energy_cost, int xp_gain)
{
    PetSlot &p = active();
    uint16_t before = p.level;
    if (coins > 0) {
        uint32_t sum = save_data().coins + (uint32_t)coins;
        if (sum > 99999) sum = 99999;
        save_data().coins = sum;
    }
    p.happy = (int8_t)clampv<int>(p.happy + happy, 0, 100);
    p.energy = (int8_t)clampv<int>(p.energy - energy_cost, 0, 100);
    p.xp += (uint32_t)xp_gain;
    touch_level(p);
    cheer_until = millis() + 1200;
    save_flush();
    return p.level != before;
}

bool pet_spend_coins(uint32_t n)
{
    if (save_data().coins < n) return false;
    save_data().coins -= n;
    save_flush();
    return true;
}

bool pet_add_coins(uint32_t n)
{
    uint32_t sum = save_data().coins + n;
    if (sum > 99999) sum = 99999;
    save_data().coins = sum;
    save_flush();
    return true;
}

uint32_t pet_offline_steps()
{
    return offline_steps;
}
