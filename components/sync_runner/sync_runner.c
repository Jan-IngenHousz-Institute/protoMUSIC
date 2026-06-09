#include "sync_runner.h"

#include "device_commands.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sync_runner"

#define SYNC_RUNNER_PERIOD_MS    10000U
#define SYNC_RUNNER_TASK_NAME    "sync_runner"
/* 8 KiB matches lua_runner. cmd_mqtt_publish_next_event heap-allocates the
 * payload buffer and cJSON tree itself, so the task stack stays light. */
#define SYNC_RUNNER_TASK_STACK   8192
#define SYNC_RUNNER_TASK_PRIO    3   /* below lua_runner (5), above idle */

/* Time to wait between publish attempts while draining, to let the broker's
 * PUBACK free the single in-flight slot before the next publish. */
#define SYNC_RUNNER_ACK_POLL_MS  100U
/* Cap on consecutive "still in flight" polls before abandoning the drain this
 * cycle (~5 s) — guards against a lost PUBACK wedging the loop. */
#define SYNC_RUNNER_MAX_ACK_POLLS 50

static TaskHandle_t s_task_handle = NULL;

/* Sync gate: pause publishing while a measurement is in progress so MQTT does
 * not compete with latency-sensitive sensor reads, AND while the device is on
 * battery (Phase 1 power gate) so we only spend the radio budget on external
 * power. Both conditions drain naturally once they clear — events stay PENDING
 * in the event_log DB meanwhile. Weak so a future power_monitor can override. */
__attribute__((weak)) bool sync_runner_is_allowed(void)
{
    return !device_commands_measurement_active() &&
           device_commands_publish_power_ok();
}

/* Publish pending events back-to-back until the queue drains, the gate closes,
 * or a PUBACK stalls. One measure_id = one message, one in flight at a time. */
static void sync_runner_drain(void)
{
    int ack_polls = 0;
    while (sync_runner_is_allowed()) {
        cmd_result_t res = cmd_mqtt_publish_next_event();
        if (res.status == ESP_OK) {
            ESP_LOGI(TAG, "%s", res.message);
            ack_polls = 0;
            vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_ACK_POLL_MS));
        } else if (res.status == ESP_ERR_INVALID_STATE) {
            /* Previous event still in flight — wait for its PUBACK. */
            if (++ack_polls > SYNC_RUNNER_MAX_ACK_POLLS) {
                ESP_LOGW(TAG, "drain stalled waiting for PUBACK");
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_ACK_POLL_MS));
        } else if (res.status == ESP_ERR_NOT_FOUND) {
            return; /* nothing left to publish */
        } else {
            /* NOT_SUPPORTED (no MQTT/persistence) or a publish error. */
            if (res.status != ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGW(TAG, "publish skipped: %s", res.message);
            }
            return;
        }
    }
}

static void sync_runner_task(void *arg)
{
    (void)arg;

    /* Stagger the first run so we don't compete with boot-time noise
     * (Wi-Fi join, MQTT TLS handshake, event_log boot scan). */
    vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_PERIOD_MS));

    while (1) {
        if (sync_runner_is_allowed()) {
            /* Burst-drain all pending events while idle (one msg per id). */
            sync_runner_drain();
        } else {
            ESP_LOGD(TAG, "sync gate closed (measurement active or on battery) — deferring");
        }

        vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_PERIOD_MS));
    }
}

esp_err_t sync_runner_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ok = xTaskCreate(
        sync_runner_task,
        SYNC_RUNNER_TASK_NAME,
        SYNC_RUNNER_TASK_STACK,
        NULL,
        SYNC_RUNNER_TASK_PRIO,
        &s_task_handle
    );
    if (ok != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "background sync started (period=%u ms)",
             (unsigned)SYNC_RUNNER_PERIOD_MS);
    return ESP_OK;
}
