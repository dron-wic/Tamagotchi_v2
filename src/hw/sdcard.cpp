#include "hw/sdcard.h"

#include "board_config.h"
#include "hw/board_bus.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <string.h>

static bool ready = false;
static bool exported = false;
static uint8_t scratch[512];

struct Bus {
    Bus() { board_bus_lock(); }
    ~Bus() { board_bus_unlock(); }
};

static void release_tft()
{
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
}

static bool usable()
{
    return ready && !exported;
}

static bool mount_card()
{
    release_tft();
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    if (ready) SD.end();
    ready = SD.begin(PIN_SD_CS, SPI, 4000000);
    return ready;
}

bool sd_init()
{
    bool ok = false;
    {
        Bus lock;
        pinMode(PIN_SD_CS, OUTPUT);
        digitalWrite(PIN_SD_CS, HIGH);
        SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
        exported = false;
        ok = mount_card();
    }
    if (!ok) {
        Serial.println("SD not found");
        return false;
    }
    Serial.println("SD ok");
    sd_ensure_dir("/backup");
    return true;
}

bool sd_ok()
{
    return usable();
}

const char *sd_unready_reason()
{
    if (exported) return "Карта у компьютера";
    return "Нет SD";
}

bool sd_share_begin()
{
    Bus lock;
    exported = false;
    if (!mount_card()) return false;
    exported = true;
    return true;
}

bool sd_share_end()
{
    bool ok = false;
    {
        Bus lock;
        exported = false;
        ok = mount_card();
    }
    if (ok) sd_ensure_dir("/backup");
    return ok;
}

uint32_t sd_sector_count()
{
    if (!ready) return 0;
    Bus lock;
    return (uint32_t)SD.numSectors();
}

uint16_t sd_sector_size()
{
    if (!ready) return 0;
    Bus lock;
    size_t n = SD.sectorSize();
    if (n > 65535) return 0;
    return (uint16_t)n;
}

static int32_t xfer(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize, bool write)
{
    Bus lock;
    if (!ready || !exported || !buffer || bufsize == 0) return 0;
    release_tft();
    const uint32_t sec = 512;
    uint32_t done = 0;
    while (done < bufsize) {
        uint32_t pos = offset + done;
        uint32_t sector = lba + pos / sec;
        uint32_t off = pos % sec;
        uint32_t chunk = sec - off;
        if (chunk > bufsize - done) chunk = bufsize - done;
        if (!write && off == 0 && chunk == sec) {
            if (!SD.readRAW(buffer + done, sector)) break;
        } else if (write && off == 0 && chunk == sec) {
            if (!SD.writeRAW(buffer + done, sector)) break;
        } else if (!write) {
            if (!SD.readRAW(scratch, sector)) break;
            memcpy(buffer + done, scratch + off, chunk);
        } else {
            if (!SD.readRAW(scratch, sector)) break;
            memcpy(scratch + off, buffer + done, chunk);
            if (!SD.writeRAW(scratch, sector)) break;
        }
        done += chunk;
    }
    return (int32_t)done;
}

int32_t sd_msc_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    return xfer(lba, offset, (uint8_t *)buffer, bufsize, false);
}

int32_t sd_msc_write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufsize)
{
    return xfer(lba, offset, (uint8_t *)buffer, bufsize, true);
}

bool sd_ensure_dir(const char *path)
{
    if (!usable() || !path || !path[0]) return false;
    Bus lock;
    release_tft();
    if (SD.exists(path)) {
        File d = SD.open(path);
        bool is_dir = d && d.isDirectory();
        if (d) d.close();
        return is_dir;
    }
    return SD.mkdir(path);
}

bool sd_write_file(const char *path, const void *data, size_t len)
{
    if (!usable() || !path || !data) return false;
    Bus lock;
    release_tft();
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t n = f.write((const uint8_t *)data, len);
    f.close();
    return n == len;
}

bool sd_read_file(const char *path, void *data, size_t len, size_t *out_n)
{
    if (!usable() || !path || !data) return false;
    Bus lock;
    release_tft();
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size_t n = f.read((uint8_t *)data, len);
    f.close();
    if (out_n) *out_n = n;
    return n > 0;
}

uint8_t *sd_read_alloc(const char *path, size_t *out_n)
{
    if (out_n) *out_n = 0;
    if (!usable() || !path) return nullptr;
    Bus lock;
    release_tft();
    File f = SD.open(path, FILE_READ);
    if (!f) return nullptr;
    size_t n = f.size();
    if (n == 0 || n > 1200000) {
        f.close();
        return nullptr;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t *)malloc(n);
    if (!buf) {
        f.close();
        return nullptr;
    }
    size_t got = f.read(buf, n);
    f.close();
    if (got != n) {
        free(buf);
        return nullptr;
    }
    if (out_n) *out_n = n;
    return buf;
}

bool sd_newest_backup(char *path_out, size_t path_cap)
{
    if (!usable() || !path_out || path_cap < 24) return false;
    Bus lock;
    release_tft();
    File dir = SD.open("/backup");
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    char best[64] = {0};
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char *name = entry.name();
            const char *base = strrchr(name, '/');
            base = base ? base + 1 : name;
            if (strncmp(base, "save_", 5) == 0) {
                size_t n = strlen(base);
                if (n > 4 && strcmp(base + n - 4, ".bin") == 0) {
                    if (!best[0] || strcmp(base, best) > 0) {
                        strncpy(best, base, sizeof(best) - 1);
                        best[sizeof(best) - 1] = 0;
                    }
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    if (!best[0]) return false;
    snprintf(path_out, path_cap, "/backup/%s", best);
    return true;
}

bool sd_delete(const char *path)
{
    if (!usable() || !path) return false;
    Bus lock;
    release_tft();
    return SD.remove(path);
}

int sd_list_photos(char paths[][64], int max_n)
{
    if (!usable() || !paths || max_n <= 0) return 0;
    Bus lock;
    release_tft();
    if (!SD.exists("/photos")) {
        if (!SD.mkdir("/photos")) return 0;
    }
    File dir = SD.open("/photos");
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    int count = 0;
    File entry = dir.openNextFile();
    while (entry && count < max_n) {
        if (!entry.isDirectory()) {
            const char *name = entry.name();
            const char *base = strrchr(name, '/');
            base = base ? base + 1 : name;
            size_t n = strlen(base);
            bool jpg = n > 4 && (strcmp(base + n - 4, ".jpg") == 0 || strcmp(base + n - 4, ".JPG") == 0);
            if (jpg) {
                snprintf(paths[count], 64, "/photos/%s", base);
                count++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    for (int i = 0; i + 1 < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(paths[j], paths[i]) > 0) {
                char tmp[64];
                strncpy(tmp, paths[i], 63);
                tmp[63] = 0;
                strncpy(paths[i], paths[j], 63);
                paths[i][63] = 0;
                strncpy(paths[j], tmp, 63);
                paths[j][63] = 0;
            }
        }
    }
    return count;
}
