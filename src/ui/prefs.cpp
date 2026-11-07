#include "ui/prefs.h"

#include "hw/audio.h"
#include "hw/power.h"
#include "storage/save.h"
#include "ui/ui_common.h"

static const uint32_t DIM_MS[] = {15000, 30000, 45000, 60000, 120000, 0};
static const char *DIM_NAME[] = {"15 с", "30 с", "45 с", "1 мин", "2 мин", "нет"};
static const uint32_t OFF_MS[] = {30000, 60000, 120000, 300000, 0};
static const char *OFF_NAME[] = {"30 с", "1 мин", "2 мин", "5 мин", "нет"};
static const uint8_t BL_LEVEL[] = {6, 10, 14, 16};
static const char *BL_NAME[] = {"Тускло", "Обычно", "Ярко", "Макс"};
static const char *TRANS_NAME[] = {"Сдвиг", "Сверху", "Шторка"};

static uint8_t clamp_idx(uint8_t idx, uint8_t n)
{
    return idx < n ? idx : 0;
}

uint8_t pref_dim_count()
{
    return sizeof(DIM_MS) / sizeof(DIM_MS[0]);
}
const char *pref_dim_name(uint8_t idx)
{
    return DIM_NAME[clamp_idx(idx, pref_dim_count())];
}
uint32_t pref_dim_ms(uint8_t idx)
{
    return DIM_MS[clamp_idx(idx, pref_dim_count())];
}

uint8_t pref_off_count()
{
    return sizeof(OFF_MS) / sizeof(OFF_MS[0]);
}
const char *pref_off_name(uint8_t idx)
{
    return OFF_NAME[clamp_idx(idx, pref_off_count())];
}
uint32_t pref_off_ms(uint8_t idx)
{
    return OFF_MS[clamp_idx(idx, pref_off_count())];
}

uint8_t pref_bl_count()
{
    return sizeof(BL_LEVEL) / sizeof(BL_LEVEL[0]);
}
const char *pref_bl_name(uint8_t idx)
{
    return BL_NAME[clamp_idx(idx, pref_bl_count())];
}
uint8_t pref_bl_level(uint8_t idx)
{
    return BL_LEVEL[clamp_idx(idx, pref_bl_count())];
}

uint8_t pref_trans_count()
{
    return sizeof(TRANS_NAME) / sizeof(TRANS_NAME[0]);
}
const char *pref_trans_name(uint8_t idx)
{
    return TRANS_NAME[clamp_idx(idx, pref_trans_count())];
}

void prefs_apply()
{
    theme_apply(save_theme());
    power_configure(pref_dim_ms(save_dim_idx()), pref_off_ms(save_off_idx()), pref_bl_level(save_bl_idx()));
    audio_set_led(save_led_flash() != 0);
}
