#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the background MQTT sync task.
 *
 * Wakes every SYNC_RUNNER_PERIOD_MS, walks the known measure types, and
 * calls cmd_mqtt_publish_unsynced() on each until either one publish is
 * accepted (one in-flight slot at a time — see device_commands.c) or no
 * pending groups remain. Idempotent; subsequent calls are no-ops.
 *
 * Power gating: each cycle checks sync_runner_is_allowed() before doing
 * anything. The default implementation always returns true; replace it
 * with a power_monitor-backed check once the solar/charge sensor lands.
 */
esp_err_t sync_runner_start(void);

/**
 * @brief Gate hook for the future power-aware policy.
 *
 * Override behaviour by re-defining as needed; today returns true so the
 * sync task runs unconditionally whenever MQTT is connected.
 */
bool sync_runner_is_allowed(void);

#ifdef __cplusplus
}
#endif
