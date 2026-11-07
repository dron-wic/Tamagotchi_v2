#include "pet/render.h"

#include "pet/sprites.h"
#include "util.h"

struct Pix {
    uint16_t *p;
    int w;
    int h;

    void hline(int x0, int x1, int y, uint16_t c)
    {
        if ((unsigned)y >= (unsigned)h) return;
        if (x0 > x1) {
            int t = x0;
            x0 = x1;
            x1 = t;
        }
        if (x1 < 0 || x0 >= w) return;
        if (x0 < 0) x0 = 0;
        if (x1 >= w) x1 = w - 1;
        uint16_t *row = p + y * w + x0;
        int n = x1 - x0 + 1;
        for (int i = 0; i < n; i++) row[i] = c;
    }

    void pset(int x, int y, uint16_t c)
    {
        if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
        p[y * w + x] = c;
    }
};

static int isqrt(int v)
{
    if (v <= 0) return 0;
    int x = v;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

static void fellipse(Pix &d, int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 1 || ry < 1) {
        d.pset(cx, cy, c);
        return;
    }
    int ry2 = ry * ry;
    for (int y = -ry; y <= ry; y++) {
        int inner = ry2 - y * y;
        int dx = (rx * isqrt(inner)) / ry;
        d.hline(cx - dx, cx + dx, cy + y, c);
    }
}

static void clear(Pix &d, uint16_t c)
{
    int n = d.w * d.h;
    for (int i = 0; i < n; i++) d.p[i] = c;
}

static int bob_of(uint8_t frame, uint8_t anim)
{
    static const int8_t s[] = {0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -3, -3, -2, -1};
    int step = s[frame % 14];
    if (anim == ANIM_HAPPY || anim == ANIM_EAT) step *= 2;
    if (anim == ANIM_SAD) return 3;
    if (anim == ANIM_SLEEP) return 2;
    return step;
}

static void blit_key(Pix &d, int dx, int dy, const uint16_t *src, int sw, int sh)
{
    for (int y = 0; y < sh; y++) {
        int yy = dy + y;
        if ((unsigned)yy >= (unsigned)d.h) continue;
        const uint16_t *row = src + y * sw;
        for (int x = 0; x < sw; x++) {
            int xx = dx + x;
            if ((unsigned)xx >= (unsigned)d.w) continue;
            uint16_t p = row[x];
            if (p == 0xF81F) continue;
            d.p[yy * d.w + xx] = p;
        }
    }
}

static void smile(Pix &d, int cx, int cy, int r, uint16_t c, bool open, bool sad)
{
    if (open) {
        fellipse(d, cx, cy + 1, r, r, rgb565(90, 40, 50));
        fellipse(d, cx, cy, r - 2, r / 2, rgb565(255, 140, 160));
        return;
    }
    int dir = sad ? -1 : 1;
    for (int x = -r; x <= r; x++) {
        int y = dir * ((x * x) / (r * 2 + 2));
        d.pset(cx + x, cy + y, c);
        d.pset(cx + x, cy + y + 1, c);
    }
}

static void eyes(Pix &d, int cx, int cy, int r, uint8_t anim, uint8_t frame, uint16_t col, uint16_t hi)
{
    int gap = r / 2 + 2;
    int ex = r / 3;
    if (ex < 4) ex = 4;
    bool blink = (frame % 26) < 2 && anim != ANIM_SLEEP;
    if (anim == ANIM_SLEEP || blink) {
        d.hline(cx - gap - ex, cx - gap + ex, cy, col);
        d.hline(cx - gap - ex, cx - gap + ex, cy + 1, col);
        d.hline(cx + gap - ex, cx + gap + ex, cy, col);
        d.hline(cx + gap - ex, cx + gap + ex, cy + 1, col);
        return;
    }
    if (anim == ANIM_HAPPY || anim == ANIM_EAT) {
        for (int i = -ex; i <= ex; i++) {
            int rise = (ex - (i < 0 ? -i : i));
            d.pset(cx - gap + i, cy + rise / 2, col);
            d.pset(cx - gap + i, cy + rise / 2 + 1, col);
            d.pset(cx + gap + i, cy + rise / 2, col);
            d.pset(cx + gap + i, cy + rise / 2 + 1, col);
        }
        return;
    }
    int ey = (anim == ANIM_HUNGRY) ? ex + 3 : ex + 1;
    fellipse(d, cx - gap, cy, ex, ey, col);
    fellipse(d, cx + gap, cy, ex, ey, col);
    fellipse(d, cx - gap - ex / 3, cy - ey / 3, 2, 2, hi);
    fellipse(d, cx + gap - ex / 3, cy - ey / 3, 2, 2, hi);
    if (anim == ANIM_SAD) {
        fellipse(d, cx + gap + ex, cy + ey, 2, 3, rgb565(120, 190, 255));
    }
}

static void plus(Pix &d, int x, int y, int r, uint16_t c)
{
    d.hline(x - r, x + r, y, c);
    for (int i = -r; i <= r; i++) d.pset(x, y + i, c);
}

struct Pal {
    uint16_t body, shade, light, ear, eye, blush, nose, muzzle;
    bool dark;
};

static Pal palette(uint8_t species)
{
    Pal p;
    if (species == 0) {
        p.body = rgb565(255, 246, 238);
        p.shade = rgb565(236, 196, 176);
        p.light = rgb565(255, 255, 255);
        p.ear = rgb565(255, 170, 190);
        p.eye = rgb565(58, 42, 52);
        p.blush = rgb565(255, 160, 184);
        p.nose = rgb565(255, 120, 150);
        p.muzzle = p.body;
        p.dark = false;
    } else {
        p.body = rgb565(48, 44, 58);
        p.shade = rgb565(28, 24, 36);
        p.light = rgb565(90, 84, 104);
        p.ear = rgb565(255, 130, 170);
        p.eye = rgb565(255, 110, 160);
        p.blush = rgb565(255, 140, 176);
        p.nose = rgb565(255, 180, 200);
        p.muzzle = rgb565(255, 246, 242);
        p.dark = true;
    }
    return p;
}

void pet_render(uint16_t *dst, int w, int h, const PetView &view)
{
    Pix d{dst, w, h};
    clear(d, view.bg);
    Pal pal = palette(view.species);
    int bob = bob_of(view.frame, view.anim);
    int cx = w / 2;
    int cy = h / 2 + 6 + bob;
    int s = w < h ? w : h;
    int head = s * 24 / 100;
    if (head < 28) head = 28;
    int brx = head + 6;
    int bry = head - 2;
    if (view.anim == ANIM_HAPPY) {
        brx += 3;
        bry -= 2;
    }
    if (view.anim == ANIM_HUNGRY) {
        brx -= 2;
        bry += 2;
    }

    fellipse(d, cx, cy + bry + 16, brx + 8, 8, rgb565(232, 176, 196));

    if (view.extra == CLOTH_WINGS) {
        uint16_t wing = rgb565(255, 255, 255);
        fellipse(d, cx - brx - 8, cy + 6, 16, 10, wing);
        fellipse(d, cx + brx + 8, cy + 6, 16, 10, wing);
        fellipse(d, cx - brx - 4, cy + 4, 8, 5, rgb565(255, 214, 230));
        fellipse(d, cx + brx + 4, cy + 4, 8, 5, rgb565(255, 214, 230));
    }
    if (view.outfit == CLOTH_CAPE) {
        fellipse(d, cx, cy + 8, brx + 14, bry + 16, rgb565(58, 48, 72));
    }

    RgbSprite spr;
    bool custom = sprites_get(view.species, &spr);
    if (custom) {
        blit_key(d, cx - spr.w / 2, cy - spr.h / 2 + 8, spr.px, spr.w, spr.h);
    } else {
        fellipse(d, cx - head - 4, cy + bry, 10, 6, pal.shade);
        fellipse(d, cx + head + 4, cy + bry, 10, 6, pal.shade);
        fellipse(d, cx, cy + 8, brx, bry, pal.shade);
        fellipse(d, cx, cy + 4, brx - 2, bry - 2, pal.body);
        fellipse(d, cx - brx / 3, cy - 4, brx / 3, bry / 3, pal.light);

        if (view.outfit != CLOTH_DRESS) {
            fellipse(d, cx - 12, cy + bry - 2, 8, 6, pal.shade);
            fellipse(d, cx + 12, cy + bry - 2, 8, 6, pal.shade);
        } else {
            uint16_t dress = view.species ? rgb565(255, 120, 168) : rgb565(255, 150, 190);
            fellipse(d, cx, cy + bry / 2, brx + 2, bry, dress);
            d.hline(cx - brx / 2, cx + brx / 2, cy + 6, rgb565(255, 255, 255));
        }

        int hy = cy - bry / 2 - head / 3;
        fellipse(d, cx - head / 2, hy - head + 6, head / 2, head / 2, pal.body);
        fellipse(d, cx + head / 2, hy - head + 6, head / 2, head / 2, pal.body);
        fellipse(d, cx - head / 2, hy - head + 8, head / 4, head / 4, pal.ear);
        fellipse(d, cx + head / 2, hy - head + 8, head / 4, head / 4, pal.ear);
        fellipse(d, cx, hy, head, head, pal.shade);
        fellipse(d, cx, hy - 1, head - 2, head - 2, pal.body);
        if (pal.dark) fellipse(d, cx, hy + head / 5, head / 2, head / 3, pal.muzzle);

        eyes(d, cx, hy - 2, head, view.anim, view.frame, pal.eye, rgb565(255, 255, 255));
        fellipse(d, cx - head / 2, hy + head / 6, 5, 3, pal.blush);
        fellipse(d, cx + head / 2, hy + head / 6, 5, 3, pal.blush);
        fellipse(d, cx, hy + 4, 3, 2, pal.nose);
        smile(d, cx, hy + head / 3, head / 5, pal.eye, view.anim == ANIM_EAT, view.anim == ANIM_SAD);

        if (view.anim == ANIM_EAT) {
            fellipse(d, cx + head, hy + head / 2, 8, 6, rgb565(214, 156, 92));
            fellipse(d, cx + head - 2, hy + head / 2 - 2, 2, 2, rgb565(92, 58, 36));
        }
    }

    int hy = cy - bry / 2 - head / 3;
    if (view.hat == CLOTH_BOW) {
        uint16_t bow = rgb565(255, 79, 147);
        fellipse(d, cx - head + 4, hy - head + 10, 8, 6, bow);
        fellipse(d, cx - head + 16, hy - head + 10, 8, 6, bow);
        fellipse(d, cx - head + 10, hy - head + 10, 3, 3, rgb565(255, 214, 120));
    } else if (view.hat == CLOTH_HAT) {
        fellipse(d, cx, hy - head - 2, head - 4, 8, rgb565(255, 120, 160));
        fellipse(d, cx, hy - head + 4, head + 6, 5, rgb565(255, 150, 186));
        fellipse(d, cx + head / 2, hy - head - 6, 4, 4, rgb565(220, 40, 70));
    } else if (view.hat == CLOTH_STAR) {
        plus(d, cx + head - 6, hy - head + 8, 6, rgb565(255, 210, 60));
        fellipse(d, cx + head - 8, hy - head + 6, 2, 2, rgb565(255, 255, 255));
    }

    if (view.anim == ANIM_SLEEP) {
        int zx = cx + head - 4;
        int zy = hy - head - 6 - (view.frame % 10);
        plus(d, zx, zy, 3, rgb565(140, 120, 170));
        plus(d, zx + 12, zy - 10, 4, rgb565(170, 150, 200));
    }
    if (view.bubbles) {
        int ph = view.frame % 16;
        for (int i = 0; i < 3; i++) {
            int by = cy - 10 - ((ph + i * 5) % 16) * 3;
            fellipse(d, cx - 28 + i * 26, by, 5, 5, rgb565(255, 255, 255));
        }
    }
    if (view.anim == ANIM_HAPPY && !custom) {
        plus(d, cx - head - 6, hy - head, 3, rgb565(255, 210, 80));
        plus(d, cx + head + 4, hy - 8, 2, rgb565(255, 160, 200));
    }
}
