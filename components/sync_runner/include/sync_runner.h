#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the background MQTT sync task — the sole MQTT publisher.
 *
 * Blocks until woken (a measurement event was stored, or a measurement burst
 * finished) or a fallback timeout fires, then drains all pending events while
 * sync_runner_is_allowed() holds: one in-flight slot at a time (see
 * device_commands.c). Registers its wake hook via device_commands_set_sync_notifier().
 * Idempotent; subsequent calls are no-ops.
 *
 * Power gating: the drain only runs while sync_runner_is_allowed() is true
 * (no measurement in progress AND device on external power). Events otherwise
 * stay PENDING and drain when the gate reopens (caught by the fallback timer).
 */
esp_err_t sync_runner_start(void);

/**
 * @brief Wake the sync task to (re)evaluate the drain. Safe before start
 *        (no-op until the task exists). Registered as the store/measurement-end
 *        notifier so publishing is event-driven, not polled.
 */
void sync_runner_notify(void);

/**
 * @brief Gate hook for the power-aware policy. Weak; returns true only when no
 *        measurement is active AND the device is on external power.
 */
bool sync_runner_is_allowed(void);

#ifdef __cplusplus
}
#endif
