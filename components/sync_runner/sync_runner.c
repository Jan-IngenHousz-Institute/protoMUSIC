#include "sync_runner.h"

#include "device_commands.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sync_runner"

#define SYNC_RUNNER_PERIOD_MS    10000U
#define SYNC_RUNNER_TASK_NAME    "sync_runner"
/* 8 KiB to match lua_runner. cmd_mqtt_publish_unsynced -> build_and_publish
 * allocates ~4 KiB of stack locals (records[8], topic[256], set_buf[512],
 * sample_inner[600], sample_esc[800], payload[1024], ...). A 4 KiB task stack
 * blew up with a Cache/MMU fault on the first publish — corruption masked as
 * an instruction fetch error after return. */
#define SYNC_RUNNER_TASK_STACK   8192
#define SYNC_RUNNER_TASK_PRIO    3   /* below lua_runner (5), above idle */

/* Types the device currently produces. New types added later (e.g. ambit
 * spectra, battery voltage) extend this list — no other code change needed. */
static const char *const SYNC_RUNNER_TYPES[] = {
    "temperature",
    "humidity",
    "pressure",
};
static const size_t SYNC_RUNNER_N_TYPES =
    sizeof(SYNC_RUNNER_TYPES) / sizeof(SYNC_RUNNER_TYPES[0]);

static TaskHandle_t s_task_handle = NULL;

/* Default gate — always permits sync. Will be replaced by a power_monitor
 * lookup once the solar/charge sensor is wired (see plan, Step 3). */
__attribute__((weak)) bool sync_runner_is_allowed(void)
{
    return true;
}

static void sync_runner_task(void *arg)
{
    (void)arg;

    /* Stagger the first run a bit so we don't compete with boot-time noise
     * (Wi-Fi join, MQTT TLS handshake, SQLite warmup). */
    vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_PERIOD_MS));

    while (1) {
        if (!sync_runner_is_allowed()) {
            ESP_LOGD(TAG, "sync gate closed — skipping cycle");
            goto sleep;
        }

        /* Walk types in order; on the first success we stop so the publish-ack
         * handler can free the in-flight slot before the next cycle. NOT_FOUND
         * means "nothing pending for this type" — try the next one. Any other
         * error (including ESP_ERR_INVALID_STATE = previous publish still in
         * flight) ends the cycle. */
        for (size_t i = 0; i < SYNC_RUNNER_N_TYPES; i++) {
            cmd_result_t res = cmd_mqtt_publish_unsynced(SYNC_RUNNER_TYPES[i]);
            if (res.status == ESP_OK) {
                ESP_LOGI(TAG, "published %s: %s", SYNC_RUNNER_TYPES[i], res.message);
                break;
            }
            if (res.status == ESP_ERR_NOT_FOUND) {
                continue;
            }
            ESP_LOGD(TAG, "%s: %s (status=%d) — ending cycle",
                     SYNC_RUNNER_TYPES[i], res.message, res.status);
            break;
        }

    sleep:
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
    ESP_LOGI(TAG, "background sync started (period=%u ms, types=%u)",
             (unsigned)SYNC_RUNNER_PERIOD_MS, (unsigned)SYNC_RUNNER_N_TYPES);
    return ESP_OK;
}
