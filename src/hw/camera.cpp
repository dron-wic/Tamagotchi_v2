#include "hw/camera.h"

#include "board_config.h"
#include "pet/pet.h"
#include "pet/sprites.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <img_converters.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static bool ready = false;
static bool flash_on = false;
static bool flash_want = false; // survives init; applied when camera becomes ready

static TaskHandle_t worker = nullptr;
static SemaphoreHandle_t frame_mu = nullptr;
static uint16_t *frame_rgb = nullptr; // QQVGA in PSRAM
static volatile bool frame_valid = false;
static volatile bool worker_run = false;
static volatile bool worker_pause = false;
static volatile bool worker_idle = true;
static volatile uint8_t drop_frames = 0;
static uint32_t frame_seq = 0;
static uint32_t frame_seen = 0;

// Vertical flip only — rot180 also mirrored left/right.
static void rgb565_vflip(uint16_t *buf, int w, int h)
{
    if (!buf || w <= 0 || h <= 0) return;
    for (int y = 0; y < h / 2; y++) {
        uint16_t *a = buf + y * w;
        uint16_t *b = buf + (h - 1 - y) * w;
        for (int x = 0; x < w; x++) {
            uint16_t t = a[x];
            a[x] = b[x];
            b[x] = t;
        }
    }
}

static const struct {
    framesize_t fs;
    const char *name;
} kPhoto[] = {
    { FRAMESIZE_QQVGA, "160x120" },
    { FRAMESIZE_QVGA, "320x240" },
    { FRAMESIZE_VGA, "640x480" },
    { FRAMESIZE_XGA, "1024x768" },
};
static const uint8_t kPhotoN = sizeof(kPhoto) / sizeof(kPhoto[0]);
static uint8_t photo_idx = 0;
static bool photo_loaded = false;

static void load_photo()
{
    if (photo_loaded) return;
    photo_loaded = true;
    Preferences p;
    if (p.begin("cam", true)) {
        photo_idx = p.getUChar("px", 0);
        p.end();
    }
    if (photo_idx >= kPhotoN) photo_idx = kPhotoN - 1;
}

uint8_t camera_photo_count()
{
    return kPhotoN;
}

uint8_t camera_photo_idx()
{
    load_photo();
    return photo_idx;
}

const char *camera_photo_name(uint8_t idx)
{
    if (idx >= kPhotoN) idx = 0;
    return kPhoto[idx].name;
}

void camera_set_photo_idx(uint8_t idx)
{
    if (idx >= kPhotoN) idx = kPhotoN - 1;
    photo_idx = idx;
    photo_loaded = true;
}

void camera_photo_reset()
{
    photo_idx = 0;
    photo_loaded = true;
    Preferences p;
    if (p.begin("cam", false)) {
        p.remove("px");
        p.end();
    }
}

static void blit_pet(uint16_t *rgb, int w, int h)
{
    RgbSprite sp;
    if (!sprites_get(pet_active(), &sp) || !sp.px || sp.w == 0 || sp.h == 0) return;
    int scale = w / 160;
    if (scale < 1) scale = 1;
    int dw = (int)sp.w * scale;
    int dh = (int)sp.h * scale;
    int x0 = (w - dw) / 2;
    int y0 = h - dh - 4 * scale;
    for (int y = 0; y < dh; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= h) continue;
        int sy = y / scale;
        if (sy >= (int)sp.h) sy = (int)sp.h - 1;
        for (int x = 0; x < dw; x++) {
            int dx = x0 + x;
            if (dx < 0 || dx >= w) continue;
            int sx = x / scale;
            if (sx >= (int)sp.w) sx = (int)sp.w - 1;
            uint16_t p = sp.px[sy * sp.w + sx];
            if (p == 0xF81F) continue;
            rgb[dy * w + dx] = p;
        }
    }
}

static CameraTune tune = {};
static bool tune_loaded = false;
static bool tune_unsaved = false;
static uint8_t tune_mask = 0;

struct TuneRtc {
    uint32_t magic;
    CameraTune tune;
};
static RTC_DATA_ATTR TuneRtc tune_rtc;
static const uint32_t TUNE_RTC_MAGIC = 0x314D4354;

static int8_t clamp_i8(int v, int lo, int hi)
{
    if (v < lo) return (int8_t)lo;
    if (v > hi) return (int8_t)hi;
    return (int8_t)v;
}

static void tune_defaults(CameraTune *t)
{
    memset(t, 0, sizeof(*t));
    t->awb = 1;
}

static void tune_clamp(CameraTune *t)
{
    t->brightness = clamp_i8(t->brightness, -2, 2);
    t->contrast = clamp_i8(t->contrast, -2, 2);
    t->saturation = clamp_i8(t->saturation, -2, 2);
    t->sharpness = clamp_i8(t->sharpness, -2, 2);
    t->ae_level = clamp_i8(t->ae_level, -2, 2);
    if (t->denoise > 8) t->denoise = 8;
    t->awb = t->awb ? 1 : 0;
    t->lenc = t->lenc ? 1 : 0;
}

static void tune_remember()
{
    tune_rtc.magic = TUNE_RTC_MAGIC;
    tune_rtc.tune = tune;
}

static void tune_load()
{
    if (tune_loaded) return;
    tune_loaded = true;
    tune_defaults(&tune);
    if (tune_rtc.magic == TUNE_RTC_MAGIC) {
        tune = tune_rtc.tune;
        tune_unsaved = true;
    } else {
        Preferences p;
        if (p.begin("cam", true)) {
            CameraTune stored;
            if (p.getBytesLength("tn") == sizeof(stored) &&
                p.getBytes("tn", &stored, sizeof(stored)) == sizeof(stored)) {
                tune = stored;
            }
            p.end();
        }
    }
    tune_clamp(&tune);
    tune_remember();
}

static void tune_save()
{
    Preferences p;
    if (!p.begin("cam", false)) return;
    p.putBytes("tn", &tune, sizeof(tune));
    p.end();
}

static void push_tune(sensor_t *s)
{
    if (!s) return;
    if (s->set_brightness) s->set_brightness(s, tune.brightness);
    if (s->set_contrast) s->set_contrast(s, tune.contrast);
    if (s->set_saturation) s->set_saturation(s, tune.saturation);
    if (s->set_sharpness) s->set_sharpness(s, tune.sharpness);
    if (s->set_ae_level) s->set_ae_level(s, tune.ae_level);
    if (s->set_denoise) s->set_denoise(s, tune.denoise);
    if (s->set_whitebal) s->set_whitebal(s, tune.awb);
    if (s->set_awb_gain) s->set_awb_gain(s, tune.awb);
    if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 1);
    if (s->set_gain_ctrl) s->set_gain_ctrl(s, 1);
    if (s->set_lenc) s->set_lenc(s, tune.lenc);
    if (s->set_dcw) s->set_dcw(s, 1);
    if (s->set_bpc) s->set_bpc(s, 1);
    if (s->set_wpc) s->set_wpc(s, 1);
}

void camera_restore_i2c()
{
    // Give the shared pins back to Wire for the touch controller.
    Wire.end();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(50);
}

static bool sccb_pins_borrow()
{
    // Move the existing SCCB controller onto the pins. Do not delete either
    // driver: reinstalling I2C blanks the panel and can reset the chip.
    return i2c_set_pin(I2C_NUM_1, PIN_I2C_SDA, PIN_I2C_SCL, true, true, I2C_MODE_MASTER) == ESP_OK;
}

static void sccb_pins_return()
{
    i2c_set_pin(I2C_NUM_0, PIN_I2C_SDA, PIN_I2C_SCL, true, true, I2C_MODE_MASTER);
}

void camera_tune_get(CameraTune *out)
{
    if (!out) return;
    tune_load();
    *out = tune;
}

void camera_tune_store(const CameraTune *in)
{
    if (!in) return;
    tune_load();
    CameraTune next = *in;
    tune_clamp(&next);
    if (next.brightness != tune.brightness) tune_mask |= 1u << 0;
    if (next.contrast != tune.contrast) tune_mask |= 1u << 1;
    if (next.saturation != tune.saturation) tune_mask |= 1u << 2;
    if (next.sharpness != tune.sharpness) tune_mask |= 1u << 3;
    if (next.ae_level != tune.ae_level) tune_mask |= 1u << 4;
    if (next.denoise != tune.denoise) tune_mask |= 1u << 5;
    if (next.awb != tune.awb) tune_mask |= 1u << 6;
    if (next.lenc != tune.lenc) tune_mask |= 1u << 7;
    if (memcmp(&tune, &next, sizeof(tune)) != 0) tune_unsaved = true;
    tune = next;
    tune_loaded = true;
    tune_remember();
}

void camera_tune_apply()
{
    tune_load();
    uint8_t mask = tune_mask;
    if (!ready || !mask) return;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    bool was = worker_pause;
    worker_pause = true;
    for (int i = 0; i < 4 && worker && !worker_idle; i++) vTaskDelay(pdMS_TO_TICKS(5));
    if (!sccb_pins_borrow()) {
        worker_pause = was;
        return;
    }
    // S3 stores this as 2^n APB cycles. 22 is about 50 ms, so a missed ACK cannot stall the UI.
    i2c_set_timeout(I2C_NUM_1, 22);
    tune_mask = 0;
    drop_frames = 2;
    if ((mask & (1u << 0)) && s->set_brightness) s->set_brightness(s, tune.brightness);
    if ((mask & (1u << 1)) && s->set_contrast) s->set_contrast(s, tune.contrast);
    if ((mask & (1u << 2)) && s->set_saturation) s->set_saturation(s, tune.saturation);
    if ((mask & (1u << 3)) && s->set_sharpness) s->set_sharpness(s, tune.sharpness);
    if ((mask & (1u << 4)) && s->set_ae_level) s->set_ae_level(s, tune.ae_level);
    if ((mask & (1u << 5)) && s->set_denoise) s->set_denoise(s, tune.denoise);
    if (mask & (1u << 6)) {
        if (s->set_whitebal) s->set_whitebal(s, tune.awb);
        if (s->set_awb_gain) s->set_awb_gain(s, tune.awb);
    }
    if ((mask & (1u << 7)) && s->set_lenc) s->set_lenc(s, tune.lenc);
    sccb_pins_return();
    worker_pause = was;
}

bool camera_tune_save()
{
    tune_load();
    load_photo();
    bool was = ready;
    if (was) camera_stop();
    bool ok = false;
    Preferences p;
    if (p.begin("cam", false)) {
        ok = p.putBytes("tn", &tune, sizeof(tune)) == sizeof(tune);
        ok = p.putUChar("px", photo_idx) == 1 && ok;
        p.end();
    }
    if (ok) tune_unsaved = false;
    if (was && !camera_start()) ok = false;
    return ok;
}

void camera_tune_reset()
{
    tune_defaults(&tune);
    tune_loaded = true;
    tune_unsaved = false;
    tune_rtc.magic = 0;
    tune_save();
}

static void scale_copy(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh)
{
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)sw * sh * 2);
        return;
    }
    for (int y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        const uint16_t *row = src + sy * sw;
        uint16_t *out = dst + y * dw;
        for (int x = 0; x < dw; x++) out[x] = row[x * sw / dw];
    }
}

static void worker_fn(void *arg)
{
    (void)arg;
    while (worker_run) {
        if (worker_pause) {
            worker_idle = true;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        worker_idle = false;
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb && drop_frames) {
            drop_frames--;
            esp_camera_fb_return(fb);
            continue;
        }
        if (fb && fb->format == PIXFORMAT_RGB565 && frame_rgb && frame_mu) {
            rgb565_vflip((uint16_t *)fb->buf, fb->width, fb->height);
            if (xSemaphoreTake(frame_mu, pdMS_TO_TICKS(20)) == pdTRUE) {
                scale_copy((const uint16_t *)fb->buf, fb->width, fb->height, frame_rgb, 160, 120);
                frame_seq++;
                frame_valid = true;
                xSemaphoreGive(frame_mu);
            }
            esp_camera_fb_return(fb);
        } else if (fb) {
            esp_camera_fb_return(fb);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    worker_idle = true;
    worker = nullptr;
    vTaskDelete(nullptr);
}

static void stop_worker()
{
    if (!worker) return;
    // Leave esp_camera_fb_get before deinit. Deleting the driver under that call reboots.
    worker_pause = true;
    for (int i = 0; i < 100 && !worker_idle; i++) vTaskDelay(pdMS_TO_TICKS(10));
    worker_run = false;
    for (int i = 0; i < 50 && worker; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

bool camera_ok()
{
    return ready;
}

static bool camera_open(framesize_t fs, int fb_count, bool with_worker)
{
    if (!frame_mu) frame_mu = xSemaphoreCreateMutex();
    if (!frame_rgb) {
        frame_rgb = (uint16_t *)heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame_rgb) frame_rgb = (uint16_t *)malloc(160 * 120 * 2);
    }
    if (!frame_mu || !frame_rgb) return false;

    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = PIN_CAM_Y2;
    cfg.pin_d1 = PIN_CAM_Y3;
    cfg.pin_d2 = PIN_CAM_Y4;
    cfg.pin_d3 = PIN_CAM_Y5;
    cfg.pin_d4 = PIN_CAM_Y6;
    cfg.pin_d5 = PIN_CAM_Y7;
    cfg.pin_d6 = PIN_CAM_Y8;
    cfg.pin_d7 = PIN_CAM_Y9;
    cfg.pin_xclk = PIN_CAM_XCLK;
    cfg.pin_pclk = PIN_CAM_PCLK;
    cfg.pin_vsync = PIN_CAM_VSYNC;
    cfg.pin_href = PIN_CAM_HREF;
    cfg.pin_sccb_sda = PIN_CAM_SIOD;
    cfg.pin_sccb_scl = PIN_CAM_SIOC;
    cfg.pin_pwdn = PIN_CAM_PWDN;
    cfg.pin_reset = PIN_CAM_RESET;
    cfg.xclk_freq_hz = CAM_XCLK_HZ;
    cfg.pixel_format = PIXFORMAT_RGB565;
    cfg.frame_size = fs;
    cfg.jpeg_quality = 12;
    cfg.fb_count = fb_count;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        camera_restore_i2c();
        Serial.printf("Camera init fail %d\n", (int)err);
        ready = false;
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // No sensor flip — a previous 180° (vflip+hmirror) looked "double flipped".
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
        tune_load();
        push_tune(s);
        Serial.printf("Camera PID 0x%04X size %d xclk %u\n", s->id.PID, (int)fs, (unsigned)CAM_XCLK_HZ);
    }
    camera_restore_i2c();

    ledcSetup(2, 5000, 8);
    ledcAttachPin(PIN_LED, 2);
    flash_on = flash_want;
    ledcWrite(2, flash_on ? 255 : 0);

    frame_valid = false;
    worker_pause = false;
    if (with_worker) {
        worker_run = true;
        // Core 0 — keep Arduino loop / LVGL on core 1.
        xTaskCreatePinnedToCore(worker_fn, "cam", 4096, nullptr, 1, &worker, 0);
    }

    ready = true;
    Serial.println(with_worker ? "Camera ok (bg preview)" : "Camera ok (still)");
    return true;
}

bool camera_start()
{
    if (ready) return true;
    return camera_open(FRAMESIZE_QQVGA, 2, true);
}

void camera_stop()
{
    if (!ready) return;
    stop_worker();
    flash_on = false;
    ledcWrite(2, 0);
    ledcDetachPin(PIN_LED);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    esp_camera_deinit();
    camera_restore_i2c();
    ready = false;
    worker_idle = true;
    frame_valid = false;
    frame_seq = 0;
    frame_seen = 0;
    Serial.println("Camera stop");
}

void camera_set_flash(bool on)
{
    // Remember even before init — button can be pressed while camera is still starting.
    flash_want = on;
    if (!ready) {
        flash_on = false;
        return;
    }
    flash_on = on;
    // Full duty — duty 40 was barely visible. Re-attach in case audio GPIO stole the pin.
    ledcAttachPin(PIN_LED, 2);
    ledcWrite(2, flash_on ? 255 : 0);
}

bool camera_flash_on()
{
    return flash_on;
}

bool camera_preview(uint16_t *dst, int dst_w, int dst_h)
{
    if (!ready || !dst || !frame_valid || !frame_rgb || !frame_mu) return false;
    if (xSemaphoreTake(frame_mu, 0) != pdTRUE) return false;
    if (frame_seq == frame_seen) {
        xSemaphoreGive(frame_mu);
        return false;
    }
    scale_copy(frame_rgb, 160, 120, dst, dst_w, dst_h);
    frame_seen = frame_seq;
    xSemaphoreGive(frame_mu);
    return true;
}

bool camera_grab_still(uint16_t *dst, int dst_w, int dst_h)
{
    if (!dst) return false;
    bool started = ready;
    if (!started && !camera_start()) return false;

    worker_pause = true;
    vTaskDelay(pdMS_TO_TICKS(50));

    camera_fb_t *fb = esp_camera_fb_get();
    bool ok = false;
    if (fb && fb->format == PIXFORMAT_RGB565) {
        scale_copy((const uint16_t *)fb->buf, fb->width, fb->height, dst, dst_w, dst_h);
        ok = true;
        esp_camera_fb_return(fb);
    } else if (fb) {
        esp_camera_fb_return(fb);
    }

    worker_pause = false;
    camera_restore_i2c();
    if (!started) camera_stop();
    return ok;
}

bool camera_capture_jpeg(uint8_t **jpg, size_t *jpg_len, bool overlay_pet)
{
    if (!jpg || !jpg_len) return false;
    *jpg = nullptr;
    *jpg_len = 0;

    load_photo();
    framesize_t fs = kPhoto[photo_idx].fs;
    bool hi = fs != FRAMESIZE_QQVGA;
    bool was_live = ready;

    if (hi) {
        if (ready) camera_stop();
        if (!camera_open(fs, 1, false)) {
            if (was_live) camera_start();
            return false;
        }
    } else if (!ready && !camera_start()) {
        return false;
    } else {
        worker_pause = true;
    }
    vTaskDelay(pdMS_TO_TICKS(hi ? 150 : 80));

    int drops = hi ? 3 : 2;
    for (int i = 0; i < drops; i++) {
        camera_fb_t *drop = esp_camera_fb_get();
        if (drop) esp_camera_fb_return(drop);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_RGB565) {
        if (fb) esp_camera_fb_return(fb);
        if (hi) {
            camera_stop();
            if (was_live) camera_start();
        } else {
            worker_pause = false;
            camera_restore_i2c();
            if (!was_live) camera_stop();
        }
        return false;
    }

    rgb565_vflip((uint16_t *)fb->buf, fb->width, fb->height);
    if (overlay_pet) blit_pet((uint16_t *)fb->buf, fb->width, fb->height);

    // The camera ISR is not in IRAM. JPEG of a large frame keeps the cache busy
    // long enough for the interrupt watchdog to reset the chip, so the DMA has
    // to be gone before the encode. The preview stays off until the caller restarts it.
    uint16_t *copy = nullptr;
    int copy_w = 0;
    int copy_h = 0;
    if (hi) {
        size_t nbytes = (size_t)fb->width * fb->height * 2;
        copy = (uint16_t *)heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!copy) copy = (uint16_t *)malloc(nbytes);
        if (copy) {
            memcpy(copy, fb->buf, nbytes);
            copy_w = fb->width;
            copy_h = fb->height;
        }
    }

    uint8_t quality = fb->width >= 1280 ? 18 : 12;
    bool ok = false;
    if (copy) {
        esp_camera_fb_return(fb);
        fb = nullptr;
        camera_stop();
        camera_fb_t still = {};
        still.buf = (uint8_t *)copy;
        still.len = (size_t)copy_w * copy_h * 2;
        still.width = copy_w;
        still.height = copy_h;
        still.format = PIXFORMAT_RGB565;
        ok = frame2jpg(&still, quality, jpg, jpg_len);
    } else {
        ok = frame2jpg(fb, quality, jpg, jpg_len);
        esp_camera_fb_return(fb);
        if (hi) camera_stop();
        else {
            worker_pause = false;
            camera_restore_i2c();
            if (!was_live) camera_stop();
        }
    }
    if (copy) free(copy);

    if (!ok || !*jpg || !*jpg_len) {
        if (*jpg) {
            free(*jpg);
            *jpg = nullptr;
        }
        *jpg_len = 0;
        return false;
    }
    return true;
}
