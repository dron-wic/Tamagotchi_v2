#include "ui/screens.h"

#include "board_config.h"
#include "hw/camera.h"
#include "hw/display.h"
#include "hw/power.h"
#include "hw/sdcard.h"
#include "pet/pet.h"
#include "storage/save.h"
#include "ui/font_ru.h"
#include "ui/ui.h"
#include "ui/ui_common.h"
#include "util.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// 4:3, almost the panel width, with a gap so the frame stays on screen.
static const int PREV_W = 204;
static const int PREV_H = 153;
static const int PREV_X = (LCD_W - PREV_W) / 2;
static const int PREV_Y = 52;

static lv_obj_t *cam_scr = nullptr;
static lv_obj_t *cam_frame = nullptr;
static lv_obj_t *cam_canvas = nullptr;
static uint16_t *cam_buf = nullptr;
static lv_obj_t *cam_status = nullptr;
static bool cam_live = false;
static int cam_refs = 0;
static bool want_start = false;
static bool tune_dirty = false;
static bool want_tune_save = false;
static bool want_snap = false;
static bool mirror_on = false;
static uint32_t mirror_next = 0;
static int mirror_prev_luma = -1;

static lv_obj_t *tune_scr = nullptr;
static lv_obj_t *tune_canvas = nullptr;
static uint16_t *tune_buf = nullptr;
static bool tune_live = false;
static lv_obj_t *tune_lbl[6] = {};
static lv_obj_t *btn_awb = nullptr;
static lv_obj_t *btn_lenc = nullptr;
static lv_obj_t *btn_flash = nullptr;
static lv_obj_t *btn_mirror = nullptr;
static lv_obj_t *lbl_photo_sz = nullptr;

static lv_obj_t *alb_scr = nullptr;
static char alb_paths[24][64];
static int alb_count = 0;
static int alb_idx = 0;
static lv_obj_t *alb_label = nullptr;
static lv_obj_t *alb_frame = nullptr;
static lv_obj_t *alb_canvas = nullptr;
static uint16_t *alb_buf = nullptr;

static void set_status(const char *text)
{
    ui_set_text(cam_status, text);
}

static void cam_acquire()
{
    cam_refs++;
}

static void cam_release()
{
    if (cam_refs > 0) cam_refs--;
    if (cam_refs == 0) camera_stop();
}

static void mount_preview(lv_obj_t *scr, lv_obj_t **frame, lv_obj_t **canvas, uint16_t **buf)
{
    *buf = (uint16_t *)ps_alloc((size_t)PREV_W * PREV_H * 2);
    *frame = lv_obj_create(scr);
    lv_obj_set_pos(*frame, PREV_X - 3, PREV_Y - 3);
    lv_obj_set_size(*frame, PREV_W + 6, PREV_H + 6);
    lv_obj_set_style_bg_color(*frame, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(*frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(*frame, 2, 0);
    lv_obj_set_style_border_color(*frame, lv_color_hex(col.line), 0);
    lv_obj_set_style_radius(*frame, 4, 0);
    lv_obj_set_style_pad_all(*frame, 0, 0);
    lv_obj_clear_flag(*frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    *canvas = lv_canvas_create(scr);
    lv_obj_set_pos(*canvas, PREV_X, PREV_Y);
    lv_obj_clear_flag(*canvas, LV_OBJ_FLAG_CLICKABLE);
    if (*buf) {
        memset(*buf, 0, (size_t)PREV_W * PREV_H * 2);
        lv_canvas_set_buffer(*canvas, *buf, PREV_W, PREV_H, LV_IMG_CF_TRUE_COLOR);
    }
}

static void pump_preview(uint16_t *buf, lv_obj_t *canvas)
{
    if (!buf || !canvas || !camera_ok()) return;
    if (camera_preview(buf, PREV_W, PREV_H)) lv_obj_invalidate(canvas);
}

static void do_snap()
{
    if (!sd_ok()) {
        ui_toast(sd_unready_reason());
        set_status(camera_ok() ? sd_unready_reason() : "Нет камеры");
        return;
    }
    if (!camera_ok()) {
        ui_toast("Нет камеры");
        set_status("Нет камеры");
        return;
    }

    power_set_busy(true);
    set_status("Снимаю...");
    camera_set_flash(true);
    delay(60);

    uint8_t *jpg = nullptr;
    size_t jpg_len = 0;
    bool ok = camera_capture_jpeg(&jpg, &jpg_len, true);
    camera_set_flash(false);

    if (ok && !sd_ensure_dir("/photos")) {
        free(jpg);
        jpg = nullptr;
        ok = false;
        set_status("Нет SD");
        ui_toast("Нет SD");
    } else if (ok) {
        char path[48];
        uint32_t stamp = save_data().last_unix ? save_data().last_unix : (1000000000u + millis() / 1000u);
        snprintf(path, sizeof(path), "/photos/p_%010lu.jpg", (unsigned long)stamp);
        ok = sd_write_file(path, jpg, jpg_len);
        free(jpg);
        if (ok) {
            set_status("Сохранено");
            ui_toast("На SD");
        } else {
            set_status("Ошибка SD");
            ui_toast("Ошибка записи");
        }
    } else {
        set_status("Ошибка кадра");
        ui_toast("Сбой съёмки");
    }

    power_set_busy(false);
    // Large stills stop the sensor before JPEG. Bring the preview back after the card write.
    if (!camera_ok()) camera_start();
}

static void on_cam_del(lv_event_t *e)
{
    if (lv_event_get_target(e) != cam_scr) return;
    want_snap = false;
    cam_live = false;
    cam_release();
    cam_scr = nullptr;
    cam_frame = nullptr;
    cam_canvas = nullptr;
    cam_status = nullptr;
    if (cam_buf) {
        ps_free(cam_buf);
        cam_buf = nullptr;
    }
}

static void on_snap(lv_event_t *e)
{
    (void)e;
    // Defer — never block inside the LVGL event callback.
    want_snap = true;
    set_status("Снимаю...");
}

static void on_album(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_ALBUM);
}

static void on_tune(lv_event_t *e);

static void set_btn_text(lv_obj_t *btn, const char *text)
{
    if (!btn) return;
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, text);
}

static void on_flash_toggle(lv_event_t *e)
{
    (void)e;
    bool on = !camera_flash_on();
    camera_set_flash(on);
    set_btn_text(btn_flash, on ? "Вспышка вкл" : "Вспышка выкл");
}

static void on_mirror_toggle(lv_event_t *e)
{
    (void)e;
    mirror_on = !mirror_on;
    mirror_prev_luma = -1;
    set_btn_text(btn_mirror, mirror_on ? "Зеркало вкл" : "Зеркало выкл");
}

static int sample_luma(const uint16_t *rgb, int w, int h)
{
    if (!rgb || w <= 0 || h <= 0) return 0;
    // Sparse sample — enough for mood, cheap on CPU.
    uint32_t sum = 0;
    uint32_t n = 0;
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            uint16_t p = rgb[y * w + x];
            uint8_t r = (p >> 11) & 0x1F;
            uint8_t g = (p >> 5) & 0x3F;
            uint8_t b = p & 0x1F;
            sum += (uint32_t)r * 2 + g + b; // rough luma
            n++;
        }
    }
    return n ? (int)(sum / n) : 0;
}

static void mirror_tick()
{
    if (!mirror_on || !cam_buf) return;
    uint32_t now = millis();
    if ((int32_t)(now - mirror_next) < 0) return;
    mirror_next = now + 700;

    int luma = sample_luma(cam_buf, PREV_W, PREV_H);
    int delta = (mirror_prev_luma < 0) ? 0 : (luma - mirror_prev_luma);
    mirror_prev_luma = luma;

    if (delta > 25 || delta < -25) {
        set_status("Ой! Испугался");
        pet_reward(0, -4, 0, 0);
    } else if (luma < 28) {
        set_status("Темно - дремлет");
    } else if (luma > 70) {
        set_status("Светло - радуется");
        static uint32_t last_boost = 0;
        if ((uint32_t)(now - last_boost) > 8000) {
            last_boost = now;
            pet_reward(0, 2, 0, 0);
        }
    } else {
        set_status("Зеркало: спокойно");
    }
}

lv_obj_t *screen_camera_create()
{
    lv_obj_t *scr = ui_screen();
    ui_back_bar(scr, "Фото");

    mount_preview(scr, &cam_frame, &cam_canvas, &cam_buf);

    int y = PREV_Y + PREV_H + 12;
    cam_status = lv_label_create(scr);
    lv_obj_set_pos(cam_status, 12, y);
    lv_obj_set_width(cam_status, LCD_W - 24);
    lv_obj_set_style_text_align(cam_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cam_status, lv_color_hex(col.ink), 0);
    if (camera_ok()) {
        lv_label_set_text(cam_status, sd_ok() ? "Наведи и сними" : sd_unready_reason());
    } else {
        lv_label_set_text(cam_status, "Включаю камеру...");
    }

    ui_button(scr, 12, y + 28, 96, 48, "Снять", on_snap, NULL);
    ui_button(scr, 114, y + 28, 96, 48, "Альбом", on_album, NULL);
    ui_button(scr, 12, y + 84, 198, 44, "Настройки", on_tune, NULL);

    cam_scr = scr;
    cam_live = true;
    cam_acquire();
    want_snap = false;
    if (!camera_ok()) want_start = true;
    lv_obj_add_event_cb(scr, on_cam_del, LV_EVENT_DELETE, NULL);
    return scr;
}

void screen_camera_tick(uint32_t dt)
{
    (void)dt;
    if (!cam_live && !tune_live) return;

    if (want_start) {
        want_start = false;
        if (!camera_ok() && !camera_start()) {
            if (cam_live) set_status("Нет камеры");
        } else if (cam_live) {
            set_status(sd_ok() ? "Наведи и сними" : sd_unready_reason());
        }
        return;
    }

    if (cam_live && want_snap) {
        want_snap = false;
        do_snap();
        return;
    }

    if (want_tune_save) {
        want_tune_save = false;
        tune_dirty = false;
        ui_toast(camera_tune_save() ? "Сохранено" : "Не сохранено");
        return;
    }

    if (tune_dirty) {
        tune_dirty = false;
        camera_tune_apply();
    }

    if (cam_live) {
        pump_preview(cam_buf, cam_canvas);
        mirror_tick();
    }
    if (tune_live) pump_preview(tune_buf, tune_canvas);
}

static void alb_clear_preview()
{
    if (alb_buf) memset(alb_buf, 0, (size_t)PREV_W * PREV_H * 2);
    if (alb_canvas) lv_obj_invalidate(alb_canvas);
}

static void alb_load_preview()
{
    alb_clear_preview();
    if (!alb_buf || alb_count <= 0) return;
    if (alb_idx < 0 || alb_idx >= alb_count) return;

    const char *path = alb_paths[alb_idx];
    size_t n = 0;
    uint8_t *jpg = sd_read_alloc(path, &n);
    if (!jpg || n < 16) {
        Serial.printf("Album read fail %s\n", path);
        ui_set_text(alb_label, "Не читается файл");
        return;
    }
    // JPEG SOI marker check
    if (jpg[0] != 0xFF || jpg[1] != 0xD8) {
        Serial.printf("Album not jpeg %s (%02X %02X)\n", path, jpg[0], jpg[1]);
        free(jpg);
        ui_set_text(alb_label, "Не JPEG");
        return;
    }

    bool ok = display_jpg_to_rgb565(jpg, n, alb_buf, PREV_W, PREV_H);
    free(jpg);
    if (ok) {
        if (alb_canvas) lv_obj_invalidate(alb_canvas);
        Serial.printf("Album preview ok %s (%u bytes)\n", path, (unsigned)n);
    } else {
        Serial.printf("Album decode fail %s\n", path);
        ui_set_text(alb_label, "Ошибка декода");
    }
}

static void alb_paint()
{
    if (!alb_label) return;
    if (alb_count <= 0) {
        ui_set_text(alb_label, "Пусто - сними фото");
        alb_clear_preview();
        return;
    }
    if (alb_idx < 0) alb_idx = 0;
    if (alb_idx >= alb_count) alb_idx = alb_count - 1;
    const char *path = alb_paths[alb_idx];
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char buf[96];
    snprintf(buf, sizeof(buf), "%d / %d  %s", alb_idx + 1, alb_count, base);
    ui_set_text(alb_label, buf);
    alb_load_preview();
}

static void alb_reload()
{
    alb_count = sd_ok() ? sd_list_photos(alb_paths, 24) : 0;
    if (alb_idx >= alb_count) alb_idx = alb_count > 0 ? alb_count - 1 : 0;
    alb_paint();
}

static void on_alb_del_scr(lv_event_t *e)
{
    if (lv_event_get_target(e) != alb_scr) return;
    alb_scr = nullptr;
    alb_label = nullptr;
    alb_frame = nullptr;
    alb_canvas = nullptr;
    if (alb_buf) {
        ps_free(alb_buf);
        alb_buf = nullptr;
    }
}

static void on_alb_prev(lv_event_t *e)
{
    (void)e;
    if (alb_count <= 0) return;
    alb_idx = (alb_idx + alb_count - 1) % alb_count;
    alb_paint();
}

static void on_alb_next(lv_event_t *e)
{
    (void)e;
    if (alb_count <= 0) return;
    alb_idx = (alb_idx + 1) % alb_count;
    alb_paint();
}

static void do_alb_delete()
{
    if (alb_count <= 0) return;
    if (!sd_delete(alb_paths[alb_idx])) {
        ui_toast("Не удалилось");
        return;
    }
    ui_toast("Удалено");
    alb_reload();
}

static void on_alb_delete(lv_event_t *e)
{
    (void)e;
    if (alb_count <= 0) {
        ui_toast("Пусто");
        return;
    }
    ui_confirm("Удалить фото?", do_alb_delete);
}

static void on_alb_camera(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_CAMERA);
}

static int tune_get(int id)
{
    CameraTune t;
    camera_tune_get(&t);
    if (id == 0) return t.ae_level;
    if (id == 1) return t.brightness;
    if (id == 2) return t.contrast;
    if (id == 3) return t.saturation;
    if (id == 4) return t.sharpness;
    return t.denoise;
}

static void tune_put(int id, int v)
{
    CameraTune t;
    camera_tune_get(&t);
    if (id == 0) t.ae_level = (int8_t)v;
    else if (id == 1) t.brightness = (int8_t)v;
    else if (id == 2) t.contrast = (int8_t)v;
    else if (id == 3) t.saturation = (int8_t)v;
    else if (id == 4) t.sharpness = (int8_t)v;
    else t.denoise = (uint8_t)v;
    camera_tune_store(&t);
    tune_dirty = true;
}

static void paint_tune_lbl(int id)
{
    if (id < 0 || id > 5 || !tune_lbl[id]) return;
    char buf[8];
    int v = tune_get(id);
    if (id == 5) snprintf(buf, sizeof(buf), "%d", v);
    else snprintf(buf, sizeof(buf), "%+d", v);
    lv_label_set_text(tune_lbl[id], buf);
}

static void on_tune_minus(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    int lo = id == 5 ? 0 : -2;
    int v = tune_get(id) - 1;
    if (v < lo) v = lo;
    tune_put(id, v);
    paint_tune_lbl(id);
}

static void on_tune_plus(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    int hi = id == 5 ? 8 : 2;
    int v = tune_get(id) + 1;
    if (v > hi) v = hi;
    tune_put(id, v);
    paint_tune_lbl(id);
}

static void on_awb(lv_event_t *e)
{
    (void)e;
    CameraTune t;
    camera_tune_get(&t);
    t.awb = t.awb ? 0 : 1;
    camera_tune_store(&t);
    tune_dirty = true;
    set_btn_text(btn_awb, t.awb ? "Баланс вкл" : "Баланс выкл");
}

static void on_lenc(lv_event_t *e)
{
    (void)e;
    CameraTune t;
    camera_tune_get(&t);
    t.lenc = t.lenc ? 0 : 1;
    camera_tune_store(&t);
    tune_dirty = true;
    set_btn_text(btn_lenc, t.lenc ? "Линза вкл" : "Линза выкл");
}

static void on_tune_save(lv_event_t *e)
{
    (void)e;
    want_tune_save = true;
}

static void step_photo_cam(int dir)
{
    int n = (int)camera_photo_count();
    int i = (int)camera_photo_idx() + dir;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    camera_set_photo_idx((uint8_t)i);
    if (lbl_photo_sz) lv_label_set_text(lbl_photo_sz, camera_photo_name(camera_photo_idx()));
}

static void on_photo_minus(lv_event_t *e)
{
    (void)e;
    step_photo_cam(-1);
}

static void on_photo_plus(lv_event_t *e)
{
    (void)e;
    step_photo_cam(1);
}

static void on_tune_del(lv_event_t *e)
{
    if (lv_event_get_target(e) != tune_scr) return;
    tune_live = false;
    cam_release();
    tune_scr = nullptr;
    tune_canvas = nullptr;
    btn_awb = btn_lenc = btn_flash = btn_mirror = lbl_photo_sz = nullptr;
    for (int i = 0; i < 6; i++) tune_lbl[i] = nullptr;
    if (tune_buf) {
        ps_free(tune_buf);
        tune_buf = nullptr;
    }
}

static void on_tune(lv_event_t *e)
{
    (void)e;
    ui_show(SCR_CAMSET);
}

static int tune_row(lv_obj_t *parent, int y, int id, const char *name)
{
    lv_obj_t *cap = lv_label_create(parent);
    lv_label_set_text(cap, name);
    lv_obj_set_pos(cap, 12, y);
    lv_obj_set_style_text_color(cap, lv_color_hex(col.ink), 0);
    y += 22;
    ui_button(parent, 12, y, 44, 44, "-", on_tune_minus, (void *)(intptr_t)id);
    ui_button(parent, 166, y, 44, 44, "+", on_tune_plus, (void *)(intptr_t)id);
    tune_lbl[id] = lv_label_create(parent);
    lv_obj_set_pos(tune_lbl[id], 60, y + 12);
    lv_obj_set_width(tune_lbl[id], 102);
    lv_obj_set_style_text_align(tune_lbl[id], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(tune_lbl[id], lv_color_hex(col.ink), 0);
    paint_tune_lbl(id);
    return y + 52;
}

lv_obj_t *screen_camset_create()
{
    lv_obj_t *scr = ui_screen();
    ui_back_bar(scr, "Камера");

    lv_obj_t *frame = nullptr;
    mount_preview(scr, &frame, &tune_canvas, &tune_buf);

    int top = PREV_Y + PREV_H + 8;
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_pos(list, 0, top);
    lv_obj_set_size(list, LCD_W, LCD_H - top);
    lv_obj_set_style_bg_color(list, lv_color_hex(col.bg), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static const char *names[] = {
        "Экспозиция", "Яркость", "Контраст", "Цвет", "Резкость", "Шум",
    };
    int y = 4;
    for (int i = 0; i < 6; i++) y = tune_row(list, y, i, names[i]);

    CameraTune cur;
    camera_tune_get(&cur);

    lv_obj_t *cap = lv_label_create(list);
    lv_label_set_text(cap, "Свет");
    lv_obj_set_pos(cap, 12, y);
    lv_obj_set_style_text_color(cap, lv_color_hex(col.ink), 0);
    y += 22;
    btn_awb = ui_button(list, 12, y, 198, 44, cur.awb ? "Баланс вкл" : "Баланс выкл", on_awb, NULL);
    y += 52;
    btn_lenc = ui_button(list, 12, y, 198, 44, cur.lenc ? "Линза вкл" : "Линза выкл", on_lenc, NULL);
    y += 56;

    cap = lv_label_create(list);
    lv_label_set_text(cap, "Съёмка");
    lv_obj_set_pos(cap, 12, y);
    lv_obj_set_style_text_color(cap, lv_color_hex(col.ink), 0);
    y += 22;
    btn_flash = ui_button(list, 12, y, 198, 44,
                          camera_flash_on() ? "Вспышка вкл" : "Вспышка выкл", on_flash_toggle, NULL);
    y += 52;
    btn_mirror = ui_button(list, 12, y, 198, 44,
                           mirror_on ? "Зеркало вкл" : "Зеркало выкл", on_mirror_toggle, NULL);
    y += 56;

    cap = lv_label_create(list);
    lv_label_set_text(cap, "Размер фото");
    lv_obj_set_pos(cap, 12, y);
    lv_obj_set_style_text_color(cap, lv_color_hex(col.ink), 0);
    y += 22;
    ui_button(list, 12, y, 44, 44, "-", on_photo_minus, NULL);
    ui_button(list, 166, y, 44, 44, "+", on_photo_plus, NULL);
    lbl_photo_sz = lv_label_create(list);
    lv_label_set_text(lbl_photo_sz, camera_photo_name(camera_photo_idx()));
    lv_obj_set_pos(lbl_photo_sz, 60, y + 12);
    lv_obj_set_width(lbl_photo_sz, 102);
    lv_obj_set_style_text_align(lbl_photo_sz, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl_photo_sz, lv_color_hex(col.ink), 0);
    y += 64;

    ui_button(list, 12, y, 198, 44, "Сохранить", on_tune_save, NULL);
    y += 56;

    lv_obj_t *tail = lv_obj_create(list);
    lv_obj_set_pos(tail, 0, y);
    lv_obj_set_size(tail, 10, 12);
    lv_obj_set_style_bg_opa(tail, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tail, 0, 0);
    lv_obj_clear_flag(tail, LV_OBJ_FLAG_CLICKABLE);

    tune_scr = scr;
    tune_live = true;
    cam_acquire();
    if (!camera_ok()) want_start = true;
    lv_obj_add_event_cb(scr, on_tune_del, LV_EVENT_DELETE, NULL);
    return scr;
}

lv_obj_t *screen_album_create()
{
    lv_obj_t *scr = ui_screen();
    ui_back_bar(scr, "Альбом");

    mount_preview(scr, &alb_frame, &alb_canvas, &alb_buf);

    alb_label = lv_label_create(scr);
    lv_obj_set_pos(alb_label, 8, PREV_Y + PREV_H + 8);
    lv_obj_set_width(alb_label, LCD_W - 16);
    lv_obj_set_style_text_align(alb_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alb_label, lv_color_hex(col.ink), 0);
    lv_label_set_long_mode(alb_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(alb_label, "...");

    // ASCII punctuation only - font_ru has no ellipsis/arrows.
    ui_button(scr, 12, PREV_Y + PREV_H + 40, 96, 44, "Назад", on_alb_prev, NULL);
    ui_button(scr, 114, PREV_Y + PREV_H + 40, 96, 44, "Далее", on_alb_next, NULL);
    ui_button(scr, 12, PREV_Y + PREV_H + 92, 198, 44, "Удалить", on_alb_delete, NULL);
    ui_button(scr, 12, PREV_Y + PREV_H + 144, 198, 44, "К камере", on_alb_camera, NULL);

    alb_scr = scr;
    alb_idx = 0;
    lv_obj_add_event_cb(scr, on_alb_del_scr, LV_EVENT_DELETE, NULL);
    alb_reload();
    if (!sd_ok()) ui_toast(sd_unready_reason());
    return scr;
}

void screen_album_tick(uint32_t dt)
{
    (void)dt;
}
