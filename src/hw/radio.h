#pragma once

#include <stdint.h>

void radio_init();
void radio_tick(uint32_t now);

// ESP-NOW listen/broadcast. Stops SoftAP/STA net while active.
bool radio_start();
void radio_stop();
bool radio_busy();
const char *radio_status();

// Broadcast a small coin gift; peers with radio_start() receive it.
bool radio_send_gift(uint8_t coins);

// Drain one pending received gift into *coins; returns false if empty.
bool radio_take_gift(uint8_t *coins);
