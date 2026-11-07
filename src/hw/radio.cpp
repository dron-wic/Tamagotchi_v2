#include "hw/radio.h"

#include "hw/net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

static const uint32_t RADIO_MAGIC = 0x414D4154u; // 'TAMA' LE
static const uint32_t RADIO_TIMEOUT_MS = 180000;
static const uint8_t GIFT_TYPE = 1;

#pragma pack(push, 1)
struct GiftPkt {
    uint32_t magic;
    uint8_t type;
    uint8_t coins;
    uint16_t pad;
    uint32_t nonce;
};
#pragma pack(pop)

static bool live = false;
static uint32_t since = 0;
static char status[40] = "ESP-NOW выкл";
static uint8_t gift_q[4];
static uint8_t gift_n = 0;
static uint32_t last_nonce = 0;

static void set_status(const char *s)
{
    snprintf(status, sizeof(status), "%s", s ? s : "");
}

static void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
    (void)mac;
    if (!data || len < (int)sizeof(GiftPkt)) return;
    GiftPkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.magic != RADIO_MAGIC || pkt.type != GIFT_TYPE) return;
    if (pkt.coins == 0 || pkt.coins > 20) return;
    if (pkt.nonce == last_nonce) return;
    last_nonce = pkt.nonce;
    if (gift_n < sizeof(gift_q)) gift_q[gift_n++] = pkt.coins;
}

static void on_sent(const uint8_t *mac, esp_now_send_status_t st)
{
    (void)mac;
    set_status(st == ESP_NOW_SEND_SUCCESS ? "Подарок ушёл" : "Не доставлено");
}

void radio_init()
{
    set_status("ESP-NOW выкл");
}

void radio_stop()
{
    if (!live) return;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    live = false;
    set_status("ESP-NOW выкл");
}

bool radio_busy()
{
    return live;
}

const char *radio_status()
{
    return status;
}

bool radio_start()
{
    if (live) return true;
    net_stop();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(40);

    if (esp_now_init() != ESP_OK) {
        set_status("ESP-NOW ошибка");
        WiFi.mode(WIFI_OFF);
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);

    // Broadcast peer.
    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(peer.peer_addr)) {
        if (esp_now_add_peer(&peer) != ESP_OK) {
            esp_now_deinit();
            WiFi.mode(WIFI_OFF);
            set_status("Peer ошибка");
            return false;
        }
    }

    live = true;
    since = millis();
    set_status("Жду друга");
    Serial.printf("ESP-NOW on MAC %s\n", WiFi.macAddress().c_str());
    return true;
}

bool radio_send_gift(uint8_t coins)
{
    if (!live) return false;
    if (coins == 0) coins = 5;
    if (coins > 20) coins = 20;

    GiftPkt pkt = {};
    pkt.magic = RADIO_MAGIC;
    pkt.type = GIFT_TYPE;
    pkt.coins = coins;
    pkt.nonce = (uint32_t)esp_random();

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t err = esp_now_send(bcast, (uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        set_status("Отправка сбой");
        return false;
    }
    set_status("Шлю подарок...");
    since = millis();
    return true;
}

bool radio_take_gift(uint8_t *coins)
{
    if (!gift_n) return false;
    if (coins) *coins = gift_q[0];
    for (uint8_t i = 1; i < gift_n; i++) gift_q[i - 1] = gift_q[i];
    gift_n--;
    return true;
}

void radio_tick(uint32_t now)
{
    if (!live) return;
    if ((uint32_t)(now - since) > RADIO_TIMEOUT_MS) {
        set_status("Таймаут");
        radio_stop();
    }
}
