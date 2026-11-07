#include "hw/audio.h"

#include "board_config.h"
#include "hw/camera.h"
#include "ui/daily.h"

#include <Arduino.h>

struct Note {
    uint16_t freq;
    uint16_t ms;
};

static Note queue[6];
static int count = 0;
static int note_pos = 0;
static bool playing = false;
static bool use_led = true;
static uint32_t until = 0;

static void silence()
{
#if PIN_BUZZER >= 0
    ledcWriteTone(4, 0);
#endif
    // Don't steal GPIO 38 while the camera torch is on.
    if (!camera_flash_on()) digitalWrite(PIN_LED, LOW);
}

static void start_note(const Note &n)
{
#if PIN_BUZZER >= 0
    ledcWriteTone(4, n.freq);
#endif
    if (use_led && !camera_flash_on() && !daily_silence_led())
        digitalWrite(PIN_LED, n.freq ? HIGH : LOW);
}

static void play(const uint16_t *freq, const uint16_t *dur, int n)
{
    if (n > 6) n = 6;
    count = n;
    note_pos = 0;
    playing = false;
    for (int i = 0; i < n; i++) {
        queue[i].freq = freq[i];
        queue[i].ms = dur[i];
    }
}

void audio_init()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
#if PIN_BUZZER >= 0
    ledcSetup(4, 2000, 8);
    ledcAttachPin(PIN_BUZZER, 4);
#endif
}

void audio_tick(uint32_t now)
{
    if (note_pos >= count) {
        if (playing) {
            silence();
            playing = false;
        }
        return;
    }
    if (!playing) {
        start_note(queue[note_pos]);
        playing = true;
        until = now + queue[note_pos].ms;
        return;
    }
    if ((int32_t)(now - until) >= 0) {
        silence();
        playing = false;
        note_pos++;
    }
}

void audio_click()
{
    static const uint16_t f[] = {1600};
    static const uint16_t d[] = {28};
    play(f, d, 1);
}

void audio_ok()
{
    static const uint16_t f[] = {784, 1175};
    static const uint16_t d[] = {70, 110};
    play(f, d, 2);
}

void audio_fail()
{
    static const uint16_t f[] = {196};
    static const uint16_t d[] = {180};
    play(f, d, 1);
}

void audio_set_led(bool on)
{
    use_led = on;
    if (!on) digitalWrite(PIN_LED, LOW);
}

void audio_coin()
{
    static const uint16_t f[] = {1046, 1568};
    static const uint16_t d[] = {40, 70};
    play(f, d, 2);
}
