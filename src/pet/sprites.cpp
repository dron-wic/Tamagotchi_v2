#include "pet/sprites.h"

#include "util.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

static uint16_t *pix[2] = {nullptr, nullptr};
static uint16_t sw[2] = {0, 0};
static uint16_t sh[2] = {0, 0};

static void load_one(uint8_t species, const char *path)
{
    if (!LittleFS.exists(path)) return;
    File f = LittleFS.open(path, "r");
    if (!f) return;
    uint8_t head[8];
    if (f.read(head, 8) != 8) {
        f.close();
        return;
    }
    if (memcmp(head, "R565", 4) != 0) {
        f.close();
        return;
    }
    uint16_t w = (uint16_t)(head[4] | (head[5] << 8));
    uint16_t h = (uint16_t)(head[6] | (head[7] << 8));
    if (w == 0 || h == 0 || w > 200 || h > 220) {
        Serial.printf("Sprite %s rejected %ux%u\n", path, w, h);
        f.close();
        return;
    }
    size_t bytes = (size_t)w * h * 2;
    if ((size_t)f.size() < bytes + 8) {
        f.close();
        return;
    }
    uint16_t *buf = (uint16_t *)ps_alloc(bytes);
    if (!buf) {
        f.close();
        return;
    }
    if (f.read((uint8_t *)buf, bytes) != bytes) {
        ps_free(buf);
        f.close();
        return;
    }
    f.close();
    pix[species] = buf;
    sw[species] = w;
    sh[species] = h;
    Serial.printf("Sprite %s %ux%u\n", path, w, h);
}

void sprites_init()
{
    if (!LittleFS.begin(false, "/littlefs", 8, "spiffs")) return;
    load_one(0, "/pet/0.rgb");
    load_one(1, "/pet/1.rgb");
}

bool sprites_get(uint8_t species, RgbSprite *out)
{
    species &= 1;
    if (!pix[species]) return false;
    out->px = pix[species];
    out->w = sw[species];
    out->h = sh[species];
    return true;
}
