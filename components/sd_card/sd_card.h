#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SD_CARD_PATH_MAX
#define SD_CARD_PATH_MAX 260
#endif

#ifndef SD_MOUNT_POINT
#define SD_MOUNT_POINT "/sdcard"
#endif

// Initializes the SD service with the board-default SDMMC wiring.
esp_err_t sdcard_init_default(void);

// Mounts the SD card once and keeps it mounted until sdcard_unmount() is called.
esp_err_t sdcard_mount(void);
esp_err_t sdcard_unmount(void);
bool sdcard_is_mounted(void);

// Write entire file content (truncate/create).
// `name` can be:
// - "data.txt" -> "/sdcard/data.txt"
// - "/sdcard/data.txt"
esp_err_t sdcard_write_file(const char *name, const char *data);

// Append raw text to a file (creates file if missing).
esp_err_t sdcard_append_file(const char *name, const char *data);

// Write one logical line (adds '\n' if not present).
esp_err_t sdcard_write_line(const char *name, const char *line);

// Append one logical line (adds '\n' if not present).
esp_err_t sdcard_append_line(const char *name, const char *line);

// Append exactly `line` followed by '\n' (creates file if missing).
esp_err_t sdcard_append_line_exact(const char *name, const char *line);

// Read one line (first line). Removes trailing '\n' and '\r' if present.
esp_err_t sdcard_read_line(const char *name, char *out, size_t out_len);

// Read 0-based line index. Returns ESP_ERR_NOT_FOUND on EOF before target line.
esp_err_t sdcard_read_line_at(const char *name, unsigned int line_index, char *out, size_t out_len);

// Check file existence. `out_exists` is set to true or false on success.
esp_err_t sdcard_file_exists(const char *name, bool *out_exists);

// Create the file if it does not exist, leave existing content untouched if it does,
// and return the resolved absolute SD path in `out_path`.
esp_err_t sdcard_ensure_file(const char *name, char *out_path, size_t out_path_len);

#ifdef __cplusplus
}
#endif
