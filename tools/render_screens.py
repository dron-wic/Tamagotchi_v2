"""Redraw docs/screenshots from the firmware layout and font_ru bitmaps."""
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_C = ROOT / "src" / "ui" / "font_ru.c"
ICONS_C = ROOT / "src" / "assets" / "icons.c"
OUT = ROOT / "docs" / "screenshots"
SCALE = 3

BG = (0xF5, 0xF5, 0xF8)
INK = (0x10, 0x10, 0x18)
CARD = (0xFF, 0xFF, 0xFF)
PRESSED = (0xD8, 0xD8, 0xE2)
TRACK = (0xE0, 0xE0, 0xE8)
HOT = (0x2F, 0x6B, 0xFF)
GOLD = (0xD4, 0x8A, 0x00)
MINT = (0x00, 0xA3, 0x6A)


def parse_fonts():
    text = FONT_C.read_text(encoding="utf-8")
    fonts = {}
    for name, lh, bl in (("font_ru_16", 21, 4), ("font_ru_22", 28, 5)):
        bm = re.search(rf"static const uint8_t {name}_bitmap\[\] = \{{(.*?)\}};", text, re.S)
        bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm.group(1)))
        dsc_block = re.search(
            rf"static const lv_font_fmt_txt_glyph_dsc_t {name}_dsc\[\] = \{{(.*?)\}};", text, re.S
        ).group(1)
        glyphs = []
        for m in re.finditer(
            r"bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)",
            dsc_block,
        ):
            glyphs.append(tuple(int(g) for g in m.groups()))
        fonts[name] = {"bitmap": bitmap, "glyphs": glyphs, "lh": lh, "bl": bl}
    return fonts


FONTS = parse_fonts()


def glyph_id(cp):
    if 32 <= cp <= 126:
        return 1 + (cp - 32)
    if 0x410 <= cp <= 0x44F:
        return 96 + (cp - 0x410)
    if cp == 0x401:
        return 160
    if cp == 0x451:
        return 161
    return 0


def bit(bitmap, index):
    return (bitmap[index >> 3] >> (7 - (index & 7))) & 1


class Screen:
    def __init__(self, w=222, h=480, bg=BG):
        self.im = Image.new("RGB", (w, h), bg)
        self.d = ImageDraw.Draw(self.im)
        self.w = w
        self.h = h

    def round(self, box, r, fill):
        self.d.rounded_rectangle(box, radius=r, fill=fill)

    def button(self, x, y, w, h, text, fill=CARD, radius=14, border=3):
        self.round((x, y, x + w - 1, y + h - 1), radius, INK)
        inset = border
        self.round((x + inset, y + inset, x + w - 1 - inset, y + h - 1 - inset), max(1, radius - inset), fill)
        self.text(text, x + 4, y, w - 8, h, "font_ru_16", INK, "center", "middle")

    def text(self, s, x, y, w, h, font, color, align="left", valign="top"):
        lines = s.split("\n")
        f = FONTS[font]
        lh = f["lh"]
        block = lh * len(lines)
        if valign == "middle":
            y0 = y + (h - block) // 2
        elif valign == "bottom":
            y0 = y + h - block
        else:
            y0 = y
        for i, line in enumerate(lines):
            tw = self.width(line, font)
            if align == "center":
                tx = x + (w - tw) // 2
            elif align == "right":
                tx = x + w - tw
            else:
                tx = x
            self.draw_line(line, tx, y0 + i * lh, font, color)

    def width(self, s, font):
        f = FONTS[font]
        total = 0
        for ch in s:
            gid = glyph_id(ord(ch))
            if gid <= 0 or gid >= len(f["glyphs"]):
                total += 8
                continue
            total += (f["glyphs"][gid][1] + 8) >> 4
        return total

    def draw_line(self, s, x, y, font, color):
        f = FONTS[font]
        pen = x
        for ch in s:
            gid = glyph_id(ord(ch))
            if gid <= 0 or gid >= len(f["glyphs"]):
                self.d.rectangle((pen, y + 2, pen + 7, y + f["lh"] - 3), outline=color)
                pen += 8
                continue
            bi, adv, bw, bh, ox, oy = f["glyphs"][gid]
            if bw and bh:
                top = y + (f["lh"] - f["bl"]) - bh - oy
                left = pen + ox
                n = 0
                for row in range(bh):
                    for col in range(bw):
                        if bit(f["bitmap"], (bi * 8) + n):
                            px, py = left + col, top + row
                            if 0 <= px < self.w and 0 <= py < self.h:
                                self.im.putpixel((px, py), color)
                        n += 1
            pen += (adv + 8) >> 4

    def bar(self, x, y, w, h, value, color):
        self.round((x, y, x + w - 1, y + h - 1), 8, INK)
        self.round((x + 2, y + 2, x + w - 3, y + h - 3), 6, TRACK)
        inner = w - 4
        fw = max(0, min(inner, inner * value // 100))
        if fw > 2:
            self.d.rounded_rectangle((x + 2, y + 2, x + 2 + fw - 1, y + h - 3), radius=6, fill=color)

    def save(self, name):
        big = self.im.resize((self.w * SCALE, self.h * SCALE), Image.Resampling.NEAREST)
        path = OUT / name
        big.save(path, "JPEG", quality=95, subsampling=0)
        print(name, big.size)


def ellipse(im, cx, cy, rx, ry, color):
    if rx < 1 or ry < 1:
        return
    d = ImageDraw.Draw(im)
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=color)


def plus(im, x, y, r, color):
    d = ImageDraw.Draw(im)
    d.line((x - r, y, x + r, y), fill=color)
    d.line((x, y - r, x, y + r), fill=color)


def pet(im, ox, oy, w, h, species=0, happy=False, bg=BG):
    # Same construction as pet_render for the default cream pet.
    px = Image.new("RGB", (w, h), bg)
    cx, cy = w // 2, h // 2 + 6
    s = min(w, h)
    head = max(28, s * 24 // 100)
    brx, bry = head + 6, head - 2
    if happy:
        brx += 3
        bry -= 2
    body = (255, 246, 238) if species == 0 else (48, 44, 58)
    shade = (236, 196, 176) if species == 0 else (28, 24, 36)
    light = (255, 255, 255) if species == 0 else (90, 84, 104)
    ear = (255, 170, 190)
    eye = (58, 42, 52) if species == 0 else (255, 110, 160)
    blush = (255, 160, 184)
    nose = (255, 120, 150) if species == 0 else (255, 180, 200)
    ellipse(px, cx, cy + bry + 16, brx + 8, 8, (232, 176, 196))
    ellipse(px, cx - head - 4, cy + bry, 10, 6, shade)
    ellipse(px, cx + head + 4, cy + bry, 10, 6, shade)
    ellipse(px, cx, cy + 8, brx, bry, shade)
    ellipse(px, cx, cy + 4, brx - 2, bry - 2, body)
    ellipse(px, cx - brx // 3, cy - 4, max(1, brx // 3), max(1, bry // 3), light)
    ellipse(px, cx - 12, cy + bry - 2, 8, 6, shade)
    ellipse(px, cx + 12, cy + bry - 2, 8, 6, shade)
    hy = cy - bry // 2 - head // 3
    ellipse(px, cx - head // 2, hy - head + 6, head // 2, head // 2, body)
    ellipse(px, cx + head // 2, hy - head + 6, head // 2, head // 2, body)
    ellipse(px, cx - head // 2, hy - head + 8, head // 4, head // 4, ear)
    ellipse(px, cx + head // 2, hy - head + 8, head // 4, head // 4, ear)
    ellipse(px, cx, hy, head, head, shade)
    ellipse(px, cx, hy - 1, head - 2, head - 2, body)
    gap = head // 2 + 2
    ex = max(4, head // 3)
    d = ImageDraw.Draw(px)
    if happy:
        for i in range(-ex, ex + 1):
            rise = ex - abs(i)
            d.point((cx - gap + i, hy - 2 + rise // 2), fill=eye)
            d.point((cx - gap + i, hy - 1 + rise // 2), fill=eye)
            d.point((cx + gap + i, hy - 2 + rise // 2), fill=eye)
            d.point((cx + gap + i, hy - 1 + rise // 2), fill=eye)
    else:
        ey = ex + 1
        ellipse(px, cx - gap, hy - 2, ex, ey, eye)
        ellipse(px, cx + gap, hy - 2, ex, ey, eye)
        ellipse(px, cx - gap - ex // 3, hy - 2 - ey // 3, 2, 2, (255, 255, 255))
        ellipse(px, cx + gap - ex // 3, hy - 2 - ey // 3, 2, 2, (255, 255, 255))
    ellipse(px, cx - head // 2, hy + head // 6, 5, 3, blush)
    ellipse(px, cx + head // 2, hy + head // 6, 5, 3, blush)
    ellipse(px, cx, hy + 4, 3, 2, nose)
    r = max(3, head // 5)
    for x in range(-r, r + 1):
        yy = (x * x) // (r * 2 + 2)
        d.point((cx + x, hy + head // 3 + yy), fill=eye)
        d.point((cx + x, hy + head // 3 + yy + 1), fill=eye)
    if happy:
        plus(px, cx - head - 6, hy - head, 3, (255, 210, 80))
        plus(px, cx + head + 4, hy - 8, 2, (255, 160, 200))
    im.paste(px, (ox, oy))


def load_icon(name):
    text = ICONS_C.read_text(encoding="utf-8")
    block = re.search(rf"static const uint16_t {name}_map\[\] = \{{(.*?)\}};", text, re.S).group(1)
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)", block)]
    im = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    for i, p in enumerate(vals[: 32 * 32]):
        if p == 0x07E0:
            continue
        r = ((p >> 11) & 31) * 255 // 31
        g = ((p >> 5) & 63) * 255 // 63
        b = (p & 31) * 255 // 31
        im.putpixel((i % 32, i // 32), (r, g, b, 255))
    return im


def paste_icon(screen, icon, x, y, zoom=256):
    size = max(1, 32 * zoom // 256)
    im = icon.resize((size, size), Image.Resampling.NEAREST)
    screen.im.paste(im, (x, y), im)


def back_bar(s, title):
    s.button(8, 8, 64, 36, "Назад")
    font = "font_ru_22" if s.width(title, "font_ru_22") <= 136 else "font_ru_16"
    lh = FONTS[font]["lh"]
    s.text(title, 78, 8 + (36 - lh) // 2, 136, lh, font, INK)


def home():
    s = Screen()
    s.text("Мими", 8, 6, 96, 28, "font_ru_22", INK)
    s.button(108, 4, 106, 34, "Настройки")
    s.round((8, 42, 35, 55), 3, INK)
    s.round((10, 44, 33, 53), 1, BG)
    s.d.rounded_rectangle((36, 45, 38, 52), radius=1, fill=INK)
    s.round((10, 44, 10 + 20 - 1, 53), 1, MINT)
    s.text("87%", 42, 42, 50, 21, "font_ru_16", INK)
    coin = load_icon("img_coin")
    paste_icon(s, coin, 94, 38, 150)
    s.text("120", 116, 42, 40, 21, "font_ru_16", INK)
    s.text("FPS 48", 160, 42, 56, 21, "font_ru_16", INK)
    pet(s.im, 21, 62, 180, 140, happy=True)
    s.text("Радуется", 16, 206, 132, 21, "font_ru_16", INK)
    s.text("21:04", 152, 206, 62, 21, "font_ru_16", INK, "right")
    s.text("Голод", 16, 230, 80, 21, "font_ru_16", INK)
    s.bar(16, 250, 190, 12, 72, GOLD)
    s.text("Счастье", 16, 268, 90, 21, "font_ru_16", INK)
    s.bar(16, 288, 190, 12, 80, HOT)
    s.button(6, 312, 104, 48, "Покормить")
    s.button(112, 312, 104, 48, "Играть")
    s.button(6, 366, 104, 48, "Магазин")
    s.button(112, 366, 104, 48, "Статус")
    s.button(6, 420, 210, 48, "Фото")
    s.save("screen-home.jpg")


def shop():
    s = Screen()
    back_bar(s, "Магазин")
    s.button(8, 50, 100, 36, "Еда")
    s.button(114, 50, 100, 36, "Одежда")
    pet(s.im, 41, 92, 140, 120, happy=True)
    s.text("Печенье\n8 монет\nв корзинке 0", 12, 214, 200, 63, "font_ru_16", INK)
    s.button(56, 286, 110, 40, "Купить")
    rows = Image.new("RGB", (206, 142), BG)
    rs = Screen()
    rs.im = rows
    rs.d = ImageDraw.Draw(rows)
    rs.w, rs.h = 206, 142
    icons = [
        ("img_cookie", "> Печенье  8", True),
        ("img_cake", "Тортик  18", False),
        ("img_apple", "Яблоко  6", False),
    ]
    y = 0
    for name, label, sel in icons:
        fill = PRESSED if sel else CARD
        rs.round((0, y, 189, y + 47), 14, INK)
        rs.round((2, y + 2, 187, y + 45), 12, fill)
        paste_icon(rs, load_icon(name), 4, y + 8, 256)
        rs.text(label, 40, y, 146, 48, "font_ru_16", INK, "left", "middle")
        y += 54
    s.im.paste(rows, (8, 332))
    s.save("screen-shop.jpg")


def games():
    s = Screen()
    back_bar(s, "Игры")
    s.text("Монеты за ловкость", 16, 58, 190, 21, "font_ru_16", INK)
    s.button(16, 100, 190, 64, "Тап-тап")
    s.button(16, 176, 190, 64, "Реакция")
    s.button(16, 252, 190, 64, "Память")
    s.button(16, 328, 190, 64, "Лови еду")
    s.text('"Действие" тоже жмёт', 16, 410, 190, 21, "font_ru_16", INK)
    s.save("screen-games.jpg")


def room_photo(w, h):
    """A low-detail viewfinder picture: wall, window light, table, cup."""
    im = Image.new("RGB", (w, h))
    pix = im.load()
    for y in range(h):
        for x in range(w):
            t = y / h
            wall = int(186 - 18 * t)
            r, g, b = wall, wall - 6, wall - 16
            # Soft window, not a hard ellipse.
            dx = (x - w * 0.72) / (w * 0.28)
            dy = (y - h * 0.22) / (h * 0.34)
            light = max(0.0, 1.0 - (dx * dx + dy * dy))
            r = int(r + light * 55)
            g = int(g + light * 62)
            b = int(b + light * 70)
            if y > int(h * 0.68):
                wood = 118 + int(14 * ((x * 3) % 7) / 7)
                k = (y - h * 0.68) / (h * 0.32)
                r = int(wood + 36 - 10 * k)
                g = int(wood - 6)
                b = int(wood - 42)
            n = ((x * 13 + y * 29) ^ (x * 7)) % 11 - 5
            pix[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))
    d = ImageDraw.Draw(im)
    # Cup on the table.
    cx, cy = int(w * 0.38), int(h * 0.74)
    d.ellipse((cx - 28, cy - 8, cx + 28, cy + 36), fill=(232, 230, 226))
    d.ellipse((cx - 22, cy - 4, cx + 22, cy + 8), fill=(90, 62, 48))
    d.arc((cx + 22, cy - 2, cx + 40, cy + 22), 300, 70, fill=(210, 208, 204), width=4)
    return im


def camera():
    s = Screen()
    back_bar(s, "Фото")
    s.round((6, 49, 215, 207), 4, INK)
    s.round((8, 51, 213, 205), 2, (0x10, 0x10, 0x10))
    s.im.paste(room_photo(204, 153), (9, 52))
    s.text("Наведи и сними", 12, 217, 198, 21, "font_ru_16", INK, "center")
    s.button(12, 245, 96, 48, "Снять")
    s.button(114, 245, 96, 48, "Альбом")
    s.button(12, 301, 198, 44, "Настройки")
    s.save("screen-camera.jpg")


def settings():
    s = Screen()
    back_bar(s, "Настройки")
    rows = [
        ("Экран", "тема и подсветка"),
        ("Питомец", "темп и уведомления"),
        ("Сеть", "Wi-Fi и друг"),
        ("Кнопки", "BOOT, 12 и 16"),
        ("Устройство", "USB и прошивка"),
    ]
    y = 56
    for title, hint in rows:
        s.button(12, y, 198, 68, f"{title}\n{hint}")
        y += 76
    s.save("screen-settings.jpg")


def status_web():
    W, H = 420, 780
    im = Image.new("RGB", (W, H), (255, 248, 242))
    d = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("segoeui.ttf", 18)
        bold = ImageFont.truetype("segoeuib.ttf", 18)
        h1 = ImageFont.truetype("segoeuib.ttf", 26)
        small = ImageFont.truetype("segoeui.ttf", 15)
    except OSError:
        font = bold = h1 = small = ImageFont.load_default()
    d.rectangle((0, 0, W, 54), fill=(32, 33, 36))
    d.rounded_rectangle((16, 12, W - 16, 42), radius=14, fill=(60, 64, 67))
    d.text((28, 16), "192.168.4.1", font=small, fill=(230, 230, 230))
    y = 78
    d.text((24, y), "Мими", font=h1, fill=(34, 34, 34))
    y += 48
    rows = [
        ("Время", "21:04 07.11"),
        ("Уровень", "3"),
        ("Монеты", "120"),
        ("Голод", "72"),
        ("Счастье", "80"),
        ("Здоровье", "100"),
        ("Энергия", "90"),
        ("Чистота", "100"),
    ]
    for name, val in rows:
        d.text((24, y), name, font=font, fill=(34, 34, 34))
        bbox = d.textbbox((0, 0), val, font=bold)
        d.text((W - 24 - (bbox[2] - bbox[0]), y), val, font=bold, fill=(34, 34, 34))
        y += 28
        d.line((24, y, W - 24, y), fill=(238, 221, 221))
        y += 10
    y += 8
    labels = ["Погладить", "Покормить", "Помыть"]
    x = 24
    for lab in labels:
        bbox = d.textbbox((0, 0), lab, font=font)
        tw = bbox[2] - bbox[0]
        d.rounded_rectangle((x, y, x + tw + 26, y + 36), radius=8, fill=(0x22, 0xAA, 0x88))
        d.text((x + 13, y + 7), lab, font=font, fill=(255, 255, 255))
        x += tw + 36
    y += 56
    d.rounded_rectangle((24, y, 196, y + 36), radius=8, fill=(0x22, 0xAA, 0x88))
    d.text((36, y + 7), "Скачать сейв", font=font, fill=(255, 255, 255))
    y += 56
    d.rounded_rectangle((24, y, 168, y + 36), radius=4, outline=(180, 180, 180), width=1, fill=(255, 255, 255))
    d.text((34, y + 7), "Выберите файл", font=small, fill=(40, 40, 40))
    d.text((180, y + 8), "Файл не выбран", font=small, fill=(90, 90, 90))
    y += 52
    d.rounded_rectangle((24, y, W - 24, y + 42), radius=8, fill=(0x22, 0xAA, 0x88))
    label = "Загрузить сейв"
    bbox = d.textbbox((0, 0), label, font=font)
    d.text(((W - (bbox[2] - bbox[0])) // 2, y + 10), label, font=font, fill=(255, 255, 255))
    y += 70
    d.text((24, y), "AP: TamaStatus · 192.168.4.1", font=small, fill=(120, 110, 100))
    im.save(OUT / "screen-status-web.jpg", "JPEG", quality=95, subsampling=0)
    print("screen-status-web.jpg", im.size)


if __name__ == "__main__":
    home()
    shop()
    games()
    camera()
    settings()
    status_web()
