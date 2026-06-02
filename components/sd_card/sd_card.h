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

// Hot-plug monitoring (no card-detect pin → software polling).
//
// sdcard_start_monitor() spawns a low-priority task that wakes every period_ms,
// probes the card with sdmmc_get_status() when mounted (CMD13 — fails on a
// pulled card) and attempts a remount when unmounted. On every mount-state
// transition it calls `cb(mounted)`, so callers (persistence, Lua) can react.
// Safe to call once after sdcard_mount(); subsequent calls are no-ops.
typedef void (*sdcard_state_cb_t)(bool mounted);
esp_err_t sdcard_start_monitor(uint32_t period_ms, sdcard_state_cb_t cb);

#ifdef __cplusplus
}
#endif
