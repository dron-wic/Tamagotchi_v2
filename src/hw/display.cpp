#include "hw/display.h"

#include <Arduino.h>

#include "hw/board_bus.h"
#include "hw/lgfx_board.hpp"
#include "hw/touch.h"
#include "ui/font_ru.h"
#include "util.h"

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <soc/gpio_struct.h>
#include <soc/spi_reg.h>

static LGFX_TDisplayS3Pro lcd;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_color_t *buf_a = nullptr;
static lv_color_t *buf_b = nullptr;

static uint32_t flush_serial = 0;
static uint32_t frame_marks = 0;
static uint32_t fps_window = 0;
static uint16_t fps_value = 0;

// LovyanGFX spi_port is SPI2_HOST + 1, which is GPSPI2.
static volatile uint32_t *const lcd_spi_cmd = (volatile uint32_t *)SPI_CMD_REG(2);

// writePixels() returns with the last chunk still shifting. Wait until that
// chunk has actually finished, then a short tail: endWrite() retunes the SPI
// for the SD card, and the panel blanks if it still sees clocks.
static void lcd_wait_inflight(int nbytes)
{
    (void)nbytes;
    uint32_t t0 = micros();
    while (*lcd_spi_cmd & (SPI_USR | SPI_UPDATE)) {
        if ((uint32_t)(micros() - t0) > 20000) break;
    }
    delayMicroseconds(20);
}

static void lcd_cs_hi()
{
    if (PIN_TFT_CS < 32) GPIO.out_w1ts = 1u << PIN_TFT_CS;
    else GPIO.out1_w1ts.val = 1u << (PIN_TFT_CS - 32);
}

static void lcd_write_rgb565(int x, int y, int w, int h, const uint16_t *px)
{
    constexpr int BAND = 8;
    lcd.startWrite();
    while (h > 0) {
        int bh = h > BAND ? BAND : h;
        lcd.setAddrWindow(x, y, w, bh);
        lcd.writePixels(px, (int32_t)w * bh, false);
        lcd_wait_inflight(w * bh * 2);
        px += (size_t)w * (size_t)bh;
        y += bh;
        h -= bh;
    }
    lcd_cs_hi();
    lcd.endWrite();
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int w = (int)(area->x2 - area->x1 + 1);
    int h = (int)(area->y2 - area->y1 + 1);
    board_bus_lock();
    lcd_write_rgb565(area->x1, area->y1, w, h, (const uint16_t *)&color_p->full);
    board_bus_unlock();
    flush_serial++;
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    TouchPoint p = touch_point();
    data->point.x = p.x;
    data->point.y = p.y;
    data->state = p.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static lv_color_t *alloc_draw(size_t pixels)
{
    size_t bytes = pixels * sizeof(lv_color_t);
    lv_color_t *p = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!p) p = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
}

void display_init()
{
    lcd.init();
    lcd.initDMA();
    lcd.setRotation(0);
    lcd.setColorDepth(16);
    lcd.fillScreen(0);

    lv_init();

    // One full frame in PSRAM. A short strip buffer flushes the scroll in
    // bands, and the gaps between bands show up as a black flash.
    size_t pixels = (size_t)LCD_W * (size_t)LCD_H;
    buf_a = (lv_color_t *)heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf_b = nullptr;
    if (buf_a) {
        Serial.printf("LVGL full buf %u\n", (unsigned)pixels);
    } else {
        const int tries[] = {48, 32, 16};
        for (int lines : tries) {
            pixels = (size_t)LCD_W * (size_t)lines;
            buf_a = alloc_draw(pixels);
            buf_b = alloc_draw(pixels);
            if (buf_a && buf_b) {
                Serial.printf("LVGL draw buf %d lines x2\n", lines);
                break;
            }
            ps_free(buf_a);
            ps_free(buf_b);
            buf_a = buf_b = nullptr;
        }
        if (!buf_a) {
            pixels = (size_t)LCD_W * 10;
            buf_a = alloc_draw(pixels);
            Serial.println("LVGL single draw buf");
        }
    }
    if (!buf_a) {
        Serial.println("LVGL buffer alloc failed");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, buf_a, buf_b, pixels);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_W;
    disp_drv.ver_res = LCD_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    lv_theme_t *theme = lv_theme_default_init(disp, lv_color_hex(0x2F6BFF), lv_color_hex(0x7A4DFF), false, &font_ru_16);
    lv_disp_set_theme(disp, theme);
    lv_disp_set_bg_color(disp, lv_color_hex(0xF5F5F8));
    lv_disp_set_bg_opa(disp, LV_OPA_COVER);

    static lv_indev_drv_t indev;
    lv_indev_drv_init(&indev);
    indev.type = LV_INDEV_TYPE_POINTER;
    indev.read_cb = touch_cb;
    // A short drag on a button should start scrolling, not wait for a long pull.
    indev.scroll_limit = 8;
    lv_indev_drv_register(&indev);

    fps_window = millis();
}

void display_task()
{
    uint32_t before = flush_serial;
    lv_timer_handler();
    if (flush_serial != before) {
        frame_marks++;
        uint32_t now = millis();
        uint32_t dt = now - fps_window;
        if (dt >= 500) {
            fps_value = (uint16_t)((frame_marks * 1000UL) / dt);
            frame_marks = 0;
            fps_window = now;
        }
    }
}

uint16_t display_fps()
{
    return fps_value;
}

void display_blit_rgb565(int x, int y, int w, int h, const uint16_t *px)
{
    if (!px || w <= 0 || h <= 0) return;
    board_bus_lock();
    lcd_write_rgb565(x, y, w, h, px);
    board_bus_unlock();
}

static bool jpeg_dims(const uint8_t *d, size_t n, int *w, int *h)
{
    if (!d || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (d[i] != 0xFF) {
            i++;
            continue;
        }
        uint8_t marker = d[i + 1];
        if (marker == 0xD8) {
            i += 2;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) break;
        if (i + 3 >= n) break;
        uint16_t seglen = (uint16_t)((d[i + 2] << 8) | d[i + 3]);
        if (seglen < 2) break;
        if (marker >= 0xC0 && marker <= 0xC3) {
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            return *w > 0 && *h > 0;
        }
        i += 2u + seglen;
    }
    return false;
}

bool display_jpg_to_rgb565(const uint8_t *jpg, size_t len, uint16_t *dst, int dw, int dh)
{
    if (!jpg || !len || !dst || dw <= 0 || dh <= 0) return false;

    lgfx::LGFX_Sprite spr(&lcd);
    spr.setColorDepth(16);
    spr.setPsram(true);
    if (!spr.createSprite(dw, dh)) {
        Serial.println("jpg sprite alloc fail");
        return false;
    }
    spr.fillScreen(0);

    int jw = 0, jh = 0;
    float scale = 1.0f;
    if (jpeg_dims(jpg, len, &jw, &jh)) {
        float sx = (float)dw / (float)jw;
        float sy = (float)dh / (float)jh;
        scale = sx < sy ? sx : sy;
        Serial.printf("jpg %dx%d -> box %dx%d scale=%.2f\n", jw, jh, dw, dh, scale);
    } else {
        Serial.println("jpg dims unknown, scale=1");
    }
    if (scale <= 0.01f) scale = 1.0f;

    // Center inside the sprite.
    int ox = 0, oy = 0;
    if (jw > 0 && jh > 0) {
        ox = (dw - (int)(jw * scale)) / 2;
        oy = (dh - (int)(jh * scale)) / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
    }

    bool ok = spr.drawJpg(jpg, (uint32_t)len, ox, oy, dw, dh, 0, 0, scale, scale);
    if (!ok) ok = spr.drawJpg(jpg, (uint32_t)len, 0, 0, dw, dh);
    if (ok) {
        spr.readRect(0, 0, dw, dh, dst);
    } else {
        Serial.printf("drawJpg fail len=%u\n", (unsigned)len);
    }
    spr.deleteSprite();
    return ok;
}
