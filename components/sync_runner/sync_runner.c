#include "sync_runner.h"

#include "device_commands.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sync_runner"

#define SYNC_RUNNER_PERIOD_MS    10000U
#define SYNC_RUNNER_TASK_NAME    "sync_runner"
/* 8 KiB matches lua_runner. cmd_mqtt_publish_next_batch heap-allocates the
 * batch buffer and cJSON tree itself, so the task stack stays light. */
#define SYNC_RUNNER_TASK_STACK   8192
#define SYNC_RUNNER_TASK_PRIO    3   /* below lua_runner (5), above idle */

static TaskHandle_t s_task_handle = NULL;

/* Default gate — always permits sync. Replaced by a power_monitor lookup once
 * the solar/charge sensor is wired (separate plan). */
__attribute__((weak)) bool sync_runner_is_allowed(void)
{
    return true;
}

static void sync_runner_task(void *arg)
{
    (void)arg;

    /* Stagger the first run so we don't compete with boot-time noise
     * (Wi-Fi join, MQTT TLS handshake, SQLite warmup). */
    vTaskDelay(pdMS_TO_TICKS(SYNC_RUNNER_PERIOD_MS));

    while (1) {
        if (sync_runner_is_allowed()) {
            cmd_result_t res = cmd_mqtt_publish_next_batch();
            if (res.status == ESP_OK) {
                ESP_LOGI(TAG, "%s", res.message);
            } else if (res.status == ESP_ERR_NOT_FOUND) {
                /* INFO once per cycle so the log shows the task is alive even
                 * when nothing is queued. Dial back to DEBUG once steady. */
                ESP_LOGI(TAG, "no pending measurements");
            } else if (res.status == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "previous batch still in flight (slot stuck?)");
            } else {
                ESP_LOGW(TAG, "publish skipped: %s", res.message);
            }
        } else {
            ESP_LOGD(TAG, "sync gate closed — skipping cycle");
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
