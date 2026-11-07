#pragma once

#include <stdbool.h>

// USB mass-storage of the microSD. Call usb_msc_init() after the card is
// mounted and before USB.begin(). The disk exists only for the boot that
// follows the settings button. A power cut clears it, so the cable is the
// USB-Serial/JTAG COM port again. Turning it on stores the flag; the caller
// reboots. Call USB.begin() only when usb_msc_active() is true.
void usb_msc_init();
void usb_msc_tick();
// False when the card cannot be shared (no SD, bad geometry).
bool usb_msc_set(bool on);
bool usb_msc_active();
// True once after the PC ejects the volume. The mode is then off.
bool usb_msc_take_eject();
void usb_msc_forget();
