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

#ifdef __cplusplus
}
#endif
