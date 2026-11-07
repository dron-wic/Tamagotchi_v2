#include "hw/net.h"

#include "hw/power.h"
#include "hw/radio.h"
#include "pet/pet.h"
#include "storage/save.h"
#include "version.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>

// Moscow without DST rules — good enough for pet ageing.
static const long TZ_OFFSET_SEC = 3 * 3600;
static const uint32_t STA_TIMEOUT_MS = 20000;
static const uint32_t NTP_TIMEOUT_MS = 12000;
static const uint32_t PORTAL_TIMEOUT_MS = 180000;
static const uint32_t STATUS_TIMEOUT_MS = 300000;
static const uint32_t OTA_TIMEOUT_MS = 600000;
// Matches the ota_0 / ota_1 slot in partitions.csv.
static const size_t OTA_SLOT_BYTES = 0x300000;

enum class Mode : uint8_t { Idle, StaWait, NtpWait, Portal, Status, Ota, Fail };

static Preferences prefs;
static WebServer *server = nullptr;
static Mode mode = Mode::Idle;
static uint32_t mode_since = 0;
static char status[48] = "Wi-Fi выкл";
static char ssid[33] = {};
static char pass[65] = {};
static bool time_ok = false;
static bool portal_got_creds = false;
static bool ota_fail = false;
static bool ota_checked = false;
static bool ota_stream = false;
static uint8_t upload_buf[sizeof(SaveData)];
static size_t upload_len = 0;

static void set_status(const char *s)
{
    snprintf(status, sizeof(status), "%s", s ? s : "");
}

static void load_creds()
{
    prefs.begin("net", true);
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pass", "");
    prefs.end();
    snprintf(ssid, sizeof(ssid), "%s", s.c_str());
    snprintf(pass, sizeof(pass), "%s", p.c_str());
}

static void save_creds(const char *s, const char *p)
{
    prefs.begin("net", false);
    prefs.putString("ssid", s ? s : "");
    prefs.putString("pass", p ? p : "");
    prefs.end();
    snprintf(ssid, sizeof(ssid), "%s", s ? s : "");
    snprintf(pass, sizeof(pass), "%s", p ? p : "");
}

static void radio_off()
{
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}

static void apply_time_ok()
{
    time_t t = time(nullptr);
    if (t > 1700000000) {
        time_ok = true;
        save_data().last_unix = (uint32_t)t;
        save_mark();
    }
}

static void begin_ntp()
{
    configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
    mode = Mode::NtpWait;
    mode_since = millis();
    set_status("NTP...");
}

static void handle_root()
{
    if (!server) return;
    static const char page[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Tama Wi-Fi</title>"
        "<style>body{font-family:sans-serif;max-width:28rem;margin:1.5rem auto;padding:0 1rem}"
        "input,button{width:100%;padding:.7rem;margin:.4rem 0;font-size:1rem}"
        "button{background:#2a8;color:#fff;border:0;border-radius:8px}</style></head><body>"
        "<h2>Wi-Fi для питомца</h2>"
        "<form method=POST action=/save>"
        "<label>Сеть (SSID)</label><input name=ssid maxlength=32 required>"
        "<label>Пароль</label><input name=pass type=password maxlength=64>"
        "<button type=submit>Сохранить и синхронизировать</button>"
        "</form><p>После сохранения устройство возьмёт время по NTP и выключит точку доступа.</p>"
        "</body></html>";
    server->send(200, "text/html", page);
}

static void handle_save()
{
    if (!server) return;
    String s = server->arg("ssid");
    String p = server->arg("pass");
    s.trim();
    if (s.length() == 0 || s.length() >= (int)sizeof(ssid)) {
        server->send(400, "text/plain", "Bad SSID");
        return;
    }
    save_creds(s.c_str(), p.c_str());
    portal_got_creds = true;
    server->send(200, "text/html",
                 "<meta charset=utf-8><p>Сохранено. Ищу сеть и NTP...</p>");
}

static void handle_save_get()
{
    if (!server) return;
    uint8_t buf[sizeof(SaveData)];
    size_t n = 0;
    if (!save_copy_out(buf, sizeof(buf), &n)) {
        server->send(500, "text/plain", "export fail");
        return;
    }
    server->sendHeader("Content-Disposition", "attachment; filename=\"tama_save.bin\"");
    server->setContentLength(n);
    server->send(200, "application/octet-stream", "");
    server->client().write(buf, n);
}

static void handle_save_post()
{
    if (!server) return;
    if (upload_len == sizeof(SaveData) && save_import_bytes(upload_buf, upload_len)) {
        server->send(200, "text/html",
                     "<meta charset=utf-8><p>Сейв загружен.</p><p><a href=/>Назад</a></p>");
    } else {
        server->send(400, "text/html",
                     "<meta charset=utf-8><p>Плохой файл сейва.</p><p><a href=/>Назад</a></p>");
    }
    upload_len = 0;
}

static void handle_save_upload()
{
    if (!server) return;
    HTTPUpload &up = server->upload();
    if (up.status == UPLOAD_FILE_START) {
        upload_len = 0;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (upload_len + up.currentSize <= sizeof(upload_buf)) {
            memcpy(upload_buf + upload_len, up.buf, up.currentSize);
            upload_len += up.currentSize;
        }
    }
}

static void handle_act_pet()
{
    if (!server) return;
    bool ok = pet_reward(0, 8, 0, 1);
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", ok ? "ok" : "fail");
}

static void handle_act_wash()
{
    if (!server) return;
    bool ok = pet_clean();
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", ok ? "ok" : "fail");
}

static void handle_act_feed()
{
    if (!server) return;
    bool ok = false;
    for (int i = 0; i < FOOD_COUNT; i++) {
        if (pet_food_count((uint8_t)i) > 0 && pet_feed((uint8_t)i)) {
            ok = true;
            break;
        }
    }
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", ok ? "ok" : "fail");
}

static void handle_status_page()
{
    if (!server) return;
    uint8_t id = pet_active();
    char clock[24];
    net_format_clock(clock, sizeof(clock), true);

    char page[2200];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta charset=utf-8>"
             "<meta http-equiv=refresh content=8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<title>Tama Status</title>"
             "<style>body{font-family:sans-serif;max-width:28rem;margin:1.5rem auto;padding:0 1rem;background:#fff8f2;color:#222}"
             "h1{font-size:1.4rem} .row{display:flex;justify-content:space-between;padding:.45rem 0;border-bottom:1px solid #edd}"
             ".v{font-weight:700} a.btn,button{display:inline-block;margin:.25rem .25rem .25rem 0;padding:.55rem .8rem;"
             "background:#2a8;color:#fff;border:0;border-radius:8px;text-decoration:none;font-size:.95rem}</style></head><body>"
             "<h1>%s</h1>"
             "<div class=row><span>Время</span><span class=v>%s</span></div>"
             "<div class=row><span>Уровень</span><span class=v>%u</span></div>"
             "<div class=row><span>Монеты</span><span class=v>%lu</span></div>"
             "<div class=row><span>Голод</span><span class=v>%d</span></div>"
             "<div class=row><span>Счастье</span><span class=v>%d</span></div>"
             "<div class=row><span>Здоровье</span><span class=v>%d</span></div>"
             "<div class=row><span>Энергия</span><span class=v>%d</span></div>"
             "<div class=row><span>Чистота</span><span class=v>%d</span></div>"
             "<p><a class=btn href=/act/pet>Погладить</a>"
             "<a class=btn href=/act/feed>Покормить</a>"
             "<a class=btn href=/act/wash>Помыть</a></p>"
             "<p><a class=btn href=/save.bin>Скачать сейв</a></p>"
             "<form method=POST action=/save.bin enctype=multipart/form-data>"
             "<input type=file name=save accept=.bin required>"
             "<button type=submit>Загрузить сейв</button></form>"
             "<p style=\"opacity:.6;margin-top:1.2rem\">AP: TamaStatus · 192.168.4.1</p>"
             "</body></html>",
             pet_name(id),
             clock,
             (unsigned)pet_level(id),
             (unsigned long)pet_coins(),
             pet_stat(id, STAT_HUNGER),
             pet_stat(id, STAT_HAPPY),
             pet_stat(id, STAT_HEALTH),
             pet_stat(id, STAT_ENERGY),
             pet_stat(id, STAT_CLEAN));
    server->send(200, "text/html", page);
}

static void handle_ota_page()
{
    if (!server) return;
    char page[2048];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<title>Tama OTA</title>"
             "<style>body{font-family:sans-serif;max-width:28rem;margin:1.5rem auto;padding:0 1rem}"
             "input,button{width:100%%;padding:.7rem;margin:.4rem 0;font-size:1rem}"
             "button{background:#246;color:#fff;border:0;border-radius:8px}</style></head><body>"
             "<h2>Обновление прошивки</h2>"
             "<p>Сейчас на устройстве: <b>%s</b></p>"
             "<form method=POST action=/update enctype=\"multipart/form-data\">"
             "<input type=file name=firmware accept=\".bin\" required>"
             "<button type=submit>Записать и перезагрузить</button>"
             "</form>"
             "<p>Файл <b>firmware.ota.bin</b> из сборки. Полный образ "
             "(firmware.full.bin) по Wi-Fi не заливается — только с компьютера.</p>"
             "<p>Не выключайте питание, пока страница не напишет, что готово.</p>"
             "</body></html>",
             FW_VERSION_STR);
    server->send(200, "text/html", page);
}

static void ota_cleanup_fail()
{
    Update.abort();
    ota_stream = false;
    ota_fail = false;
    ota_checked = false;
    power_set_busy(false);
}

static void handle_ota_done()
{
    if (!server) return;
    bool bad = ota_fail || Update.hasError();
    if (bad) {
        String why = Update.errorString();
        ota_cleanup_fail();
        server->send(400, "text/html",
                     "<meta charset=utf-8><p>Прошивка не принята.</p><p>" + why +
                         "</p><p><a href=/>Назад</a></p>");
        set_status("OTA ошибка");
        return;
    }
    if (!Update.end(true)) {
        String why = Update.errorString();
        ota_cleanup_fail();
        server->send(500, "text/html",
                     "<meta charset=utf-8><p>Запись не завершилась.</p><p>" + why +
                         "</p><p><a href=/>Назад</a></p>");
        set_status("OTA ошибка");
        return;
    }
    ota_stream = false;
    server->send(200, "text/html",
                 "<meta charset=utf-8><p>Прошивка записана. Устройство перезагружается.</p>");
    delay(400);
    ESP.restart();
}

static void handle_ota_upload()
{
    if (!server) return;
    HTTPUpload &up = server->upload();
    if (up.status == UPLOAD_FILE_START) {
        ota_fail = false;
        ota_checked = false;
        ota_stream = true;
        mode_since = millis();
        power_set_busy(true);
        set_status("OTA запись");
        Serial.printf("OTA upload '%s' total=%u\n", up.filename.c_str(), (unsigned)up.totalSize);
        if (up.totalSize > OTA_SLOT_BYTES) {
            ota_fail = true;
            Serial.println("OTA image larger than slot");
            return;
        }
        size_t slot = up.totalSize ? up.totalSize : UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(slot)) {
            ota_fail = true;
            Serial.printf("Update.begin: %s\n", Update.errorString());
        }
        return;
    }
    if (ota_fail) return;
    if (up.status == UPLOAD_FILE_WRITE) {
        if (!ota_checked) {
            ota_checked = true;
            if (up.currentSize < 1 || up.buf[0] != 0xE9) {
                ota_fail = true;
                Serial.println("OTA bad magic");
                return;
            }
        }
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            ota_fail = true;
            Serial.printf("Update.write: %s\n", Update.errorString());
            return;
        }
        mode_since = millis();
        yield();
        return;
    }
    if (up.status == UPLOAD_FILE_ABORTED) {
        Serial.println("OTA aborted");
        ota_fail = true;
        ota_stream = false;
        power_set_busy(false);
    }
}

static void handle_not_found()
{
    if (!server) return;
    // Captive-portal style redirect.
    server->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server->send(302, "text/plain", "");
}

void net_init()
{
    load_creds();
    time_ok = save_clock_now() != 0;
    if (time_ok) set_status("Часы есть");
    else set_status(ssid[0] ? "Есть SSID" : "Нет Wi-Fi");
}

void net_clear_creds()
{
    prefs.begin("net", false);
    prefs.clear();
    prefs.end();
    ssid[0] = 0;
    pass[0] = 0;
    time_ok = false;
}

void net_stop()
{
    if (ota_stream) {
        Update.abort();
        power_set_busy(false);
    }
    ota_stream = false;
    ota_fail = false;
    ota_checked = false;
    radio_off();
    mode = Mode::Idle;
    portal_got_creds = false;
    if (!time_ok) set_status(ssid[0] ? "Есть SSID" : "Wi-Fi выкл");
    else set_status("Готово");
}

bool net_busy()
{
    return mode != Mode::Idle && mode != Mode::Fail;
}

bool net_has_ssid()
{
    return ssid[0] != 0;
}

const char *net_status()
{
    return status;
}

bool net_time_ok()
{
    if (!time_ok) time_ok = save_clock_now() != 0;
    return time_ok;
}

bool net_format_clock(char *buf, size_t n, bool with_date)
{
    if (!buf || n < 6) return false;
    time_t t = time(nullptr);
    if (t < 1700000000) {
        snprintf(buf, n, "--:--");
        return false;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    if (with_date) {
        snprintf(buf, n, "%02d:%02d %02d.%02d", tm.tm_hour, tm.tm_min, tm.tm_mday, tm.tm_mon + 1);
    } else {
        snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
    return true;
}

bool net_start_sync()
{
    if (net_busy()) return false;
    load_creds();
    if (!ssid[0]) {
        set_status("Нет SSID");
        mode = Mode::Fail;
        return false;
    }
    radio_stop();
    radio_off();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, pass);
    mode = Mode::StaWait;
    mode_since = millis();
    set_status("Подключаюсь...");
    Serial.printf("WiFi STA %s\n", ssid);
    return true;
}

bool net_start_portal()
{
    if (net_busy()) return false;
    radio_stop();
    radio_off();
    portal_got_creds = false;
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP("TamaSetup", "tamagotchi");
    if (!ok) {
        set_status("AP ошибка");
        mode = Mode::Fail;
        radio_off();
        return false;
    }
    delay(80);
    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_root);
    server->on("/save", HTTP_POST, handle_save);
    server->onNotFound(handle_not_found);
    server->begin();
    mode = Mode::Portal;
    mode_since = millis();
    set_status("AP TamaSetup");
    Serial.printf("WiFi AP %s pass tamagotchi IP %s\n",
                  WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
    return true;
}

bool net_start_status()
{
    if (net_busy()) return false;
    radio_stop();
    radio_off();
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP("TamaStatus", "tamagotchi");
    if (!ok) {
        set_status("AP ошибка");
        mode = Mode::Fail;
        radio_off();
        return false;
    }
    delay(80);
    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_status_page);
    server->on("/save.bin", HTTP_GET, handle_save_get);
    server->on("/save.bin", HTTP_POST, handle_save_post, handle_save_upload);
    server->on("/act/pet", HTTP_GET, handle_act_pet);
    server->on("/act/feed", HTTP_GET, handle_act_feed);
    server->on("/act/wash", HTTP_GET, handle_act_wash);
    server->onNotFound(handle_not_found);
    server->begin();
    mode = Mode::Status;
    mode_since = millis();
    set_status("AP TamaStatus");
    Serial.printf("WiFi status AP IP %s\n", WiFi.softAPIP().toString().c_str());
    return true;
}

bool net_start_ota()
{
    if (net_busy()) return false;
    radio_stop();
    radio_off();
    ota_fail = false;
    ota_checked = false;
    ota_stream = false;
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP("TamaUpdate", "tamagotchi");
    if (!ok) {
        set_status("AP ошибка");
        mode = Mode::Fail;
        radio_off();
        return false;
    }
    delay(80);
    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_ota_page);
    server->on("/update", HTTP_POST, handle_ota_done, handle_ota_upload);
    server->onNotFound(handle_not_found);
    server->begin();
    mode = Mode::Ota;
    mode_since = millis();
    set_status("AP TamaUpdate");
    Serial.printf("OTA AP %s  IP %s\n", FW_VERSION_STR, WiFi.softAPIP().toString().c_str());
    return true;
}

void net_tick(uint32_t now)
{
    if (mode == Mode::Idle || mode == Mode::Fail) return;

    if (mode == Mode::Portal || mode == Mode::Status || mode == Mode::Ota) {
        if (server) server->handleClient();
        if (ota_stream) return;
        if (mode == Mode::Portal && portal_got_creds) {
            portal_got_creds = false;
            if (server) {
                server->stop();
                delete server;
                server = nullptr;
            }
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            WiFi.begin(ssid, pass);
            mode = Mode::StaWait;
            mode_since = now;
            set_status("Подключаюсь...");
            return;
        }
        uint32_t limit = PORTAL_TIMEOUT_MS;
        if (mode == Mode::Status) limit = STATUS_TIMEOUT_MS;
        if (mode == Mode::Ota) limit = OTA_TIMEOUT_MS;
        if ((uint32_t)(now - mode_since) > limit) {
            set_status("AP таймаут");
            radio_off();
            mode = Mode::Fail;
        }
        return;
    }

    if (mode == Mode::StaWait) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            begin_ntp();
            return;
        }
        if ((uint32_t)(now - mode_since) > STA_TIMEOUT_MS ||
            st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            set_status("Нет сети");
            radio_off();
            mode = Mode::Fail;
            Serial.println("WiFi STA fail");
        }
        return;
    }

    if (mode == Mode::NtpWait) {
        time_t t = time(nullptr);
        if (t > 1700000000) {
            apply_time_ok();
            set_status("Время ок");
            radio_off();
            mode = Mode::Idle;
            Serial.printf("NTP ok %lu\n", (unsigned long)t);
            return;
        }
        if ((uint32_t)(now - mode_since) > NTP_TIMEOUT_MS) {
            set_status("NTP сбой");
            radio_off();
            mode = Mode::Fail;
            Serial.println("NTP fail");
        }
    }
}
