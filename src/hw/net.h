#pragma once

#include <stddef.h>
#include <stdint.h>

void net_init();
void net_tick(uint32_t now);

// Non-blocking: connect STA → NTP → WiFi off. Needs SSID in prefs.
bool net_start_sync();
// SoftAP "TamaSetup" + captive form to enter Wi-Fi; then optional sync.
bool net_start_portal();
// SoftAP "TamaStatus" — live pet page in the browser.
bool net_start_status();
// SoftAP "TamaUpdate" — browser upload of firmware.ota.bin.
bool net_start_ota();
void net_stop();
void net_clear_creds();

bool net_busy();
bool net_has_ssid();
const char *net_status();

// True after a successful NTP (or valid RTC) this boot.
bool net_time_ok();
// Formats local time into buf ("21:04" / "21:04 03.11"). Returns false if unknown.
bool net_format_clock(char *buf, size_t n, bool with_date);
