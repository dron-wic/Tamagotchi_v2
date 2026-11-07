#pragma once

#include <stddef.h>
#include <stdint.h>

struct CameraTune {
    int8_t brightness;  // -2..2
    int8_t contrast;
    int8_t saturation;
    int8_t sharpness;
    int8_t ae_level;
    uint8_t denoise;    // 0..8
    uint8_t awb;        // 0 or 1
    uint8_t lenc;       // 0 or 1
};

bool camera_ok();
// Blocking init/deinit — call only from deferred UI work, not from LVGL event handlers.
bool camera_start();
void camera_stop();
// Non-blocking: copies last background frame if available.
bool camera_preview(uint16_t *dst, int dst_w, int dst_h);
bool camera_capture_jpeg(uint8_t **jpg, size_t *jpg_len, bool overlay_pet);
// Saved photo size. The viewfinder sensor frame stays 160x120.
uint8_t camera_photo_count();
uint8_t camera_photo_idx();
void camera_set_photo_idx(uint8_t idx);
const char *camera_photo_name(uint8_t idx);
void camera_photo_reset();
void camera_tune_get(CameraTune *out);
void camera_tune_store(const CameraTune *in);
void camera_tune_apply();
// Stops the sensor, writes flash, then starts the preview again.
bool camera_tune_save();
void camera_tune_reset();
void camera_set_flash(bool on);
bool camera_flash_on();
// One still into dst (blocking, runs on caller). Restores I2C afterward.
bool camera_grab_still(uint16_t *dst, int dst_w, int dst_h);
void camera_restore_i2c();
