#pragma once

#include <stddef.h>
#include <stdint.h>

bool sd_init();
bool sd_ok();
// Short reason when sd_ok() is false: card missing, or USB disk mode owns it.
const char *sd_unready_reason();
// Hand the mounted card to USB mass-storage. File APIs fail until sd_share_end().
bool sd_share_begin();
// Unmount and mount again so FatFS sees what the PC wrote.
bool sd_share_end();
uint32_t sd_sector_count();
uint16_t sd_sector_size();
int32_t sd_msc_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
int32_t sd_msc_write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufsize);
bool sd_ensure_dir(const char *path);
bool sd_write_file(const char *path, const void *data, size_t len);
bool sd_read_file(const char *path, void *data, size_t len, size_t *out_n);
// Allocates with malloc/psram; caller must free(). Returns nullptr on failure.
uint8_t *sd_read_alloc(const char *path, size_t *out_n);
// Fills path_out with newest /backup/save_*.bin (or false if none).
bool sd_newest_backup(char *path_out, size_t path_cap);
bool sd_delete(const char *path);
// Fills paths[i] with "/photos/...." ; returns count (max max_n).
int sd_list_photos(char paths[][64], int max_n);
