#include "hw/usb_msc.h"

#include "hw/sdcard.h"

#include <Arduino.h>
#include <Preferences.h>
#include <USBMSC.h>
#include <esp_system.h>

extern "C" uint8_t __wrap_tud_msc_get_maxlun_cb(void)
{
    return 0;
}

static USBMSC *disk = nullptr;
static bool active = false;
static volatile bool eject_req = false;
static bool eject_note = false;

static int32_t on_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    return sd_msc_read(lba, offset, buffer, bufsize);
}

static int32_t on_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    return sd_msc_write(lba, offset, buffer, bufsize);
}

static bool on_start_stop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    if (load_eject && !start) eject_req = true;
    return true;
}

static bool pref_on()
{
    Preferences p;
    bool on = false;
    if (p.begin("usb", true)) {
        on = p.getBool("msc", false);
        p.end();
    }
    return on;
}

static void pref_store(bool on)
{
    Preferences p;
    if (p.begin("usb", false)) {
        p.putBool("msc", on);
        p.end();
    }
}

// A power cut must come back as a COM port. The button's own reboot is a
// software reset, so the flag survives only that restart.
static bool power_was_cut()
{
    esp_reset_reason_t why = esp_reset_reason();
    return why == ESP_RST_POWERON || why == ESP_RST_BROWNOUT;
}

static void attach(USBMSC &msc)
{
    msc.vendorID("Tama");
    msc.productID("microSD");
    msc.productRevision("1.0");
    msc.onRead(on_read);
    msc.onWrite(on_write);
    msc.onStartStop(on_start_stop);
    msc.mediaPresent(false);
}

void usb_msc_init()
{
    // Must run before USB.begin(). The interface cannot be added later.
    // Leave TinyUSB off unless this boot is the one the button asked for:
    // then the USB-Serial/JTAG COM port stays on the cable.
    if (power_was_cut()) {
        if (pref_on()) pref_store(false);
        return;
    }
    if (!pref_on()) return;
    if (!sd_share_begin()) {
        pref_store(false);
        return;
    }
    uint32_t blocks = sd_sector_count();
    uint16_t bsize = sd_sector_size();
    if (blocks == 0 || bsize != 512) {
        sd_share_end();
        pref_store(false);
        return;
    }
    disk = new USBMSC();
    if (!disk) {
        sd_share_end();
        pref_store(false);
        return;
    }
    attach(*disk);
    if (!disk->begin(blocks, bsize)) {
        sd_share_end();
        pref_store(false);
        return;
    }
    disk->mediaPresent(true);
    active = true;
}

bool usb_msc_set(bool on)
{
    if (on) {
        if (active) return true;
        if (!sd_ok()) return false;
        pref_store(true);
        return true;
    }
    if (disk) {
        disk->mediaPresent(false);
        delay(30);
    }
    active = false;
    sd_share_end();
    pref_store(false);
    return true;
}

void usb_msc_tick()
{
    if (!eject_req) return;
    eject_req = false;
    if (!active) return;
    usb_msc_set(false);
    eject_note = true;
}

bool usb_msc_active()
{
    return active;
}

bool usb_msc_take_eject()
{
    if (!eject_note) return false;
    eject_note = false;
    return true;
}

void usb_msc_forget()
{
    Preferences p;
    if (p.begin("usb", false)) {
        p.remove("msc");
        p.end();
    }
    if (disk) disk->mediaPresent(false);
    active = false;
    (void)sd_share_end();
}
