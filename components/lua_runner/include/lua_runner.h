#include "esp_err.h"

#include <stdint.h>

/* Spawn the Lua runner task. Loads /sdcard/main.lua, runs it once. The task
 * self-deletes when the script returns or is stopped. Returns
 * ESP_ERR_INVALID_STATE if a task is already running. */
esp_err_t lua_runner_start(void);

/* Signal the running Lua task to exit and wait up to wait_ms for it to clean
 * up. The script is interrupted via a Lua debug hook (at the next bytecode
 * boundary) and device.sleep_ms wakes early. ESP_OK if the task stopped
 * within the budget, ESP_ERR_TIMEOUT if it is still blocked in a C call
 * (e.g. a long UART read); the task will continue and exit on its own once
 * that call returns. Safe to call when no task is running (no-op). */
esp_err_t lua_runner_stop(uint32_t wait_ms);
