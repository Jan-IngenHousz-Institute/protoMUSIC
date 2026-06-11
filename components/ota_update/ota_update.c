#include "ota_update.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#define TAG "ota_update"

#define OTA_TASK_STACK     8192
#define OTA_TASK_PRIO      4          /* below lua_runner(10); OTA is not latency-critical */
#define OTA_URL_MAX        256
#define OTA_ID_MAX         64
#define OTA_CONFIRM_TIMEOUT_S 300     /* wait this long for MQTT before rolling back a new image */
#define OTA_CONNECT_POLL_MS   1000

#define NVS_NS      "ota_upd"
#define KEY_PENDING "pending_id"      /* id of an applied-but-unconfirmed image (reboot correlation) */

typedef struct {
    char url[OTA_URL_MAX];
    char id[OTA_ID_MAX];
} ota_request_t;

static ota_update_config_t s_cfg;
static QueueHandle_t       s_queue;
static TaskHandle_t        s_task;

/* ── status reporting ──────────────────────────────────────────────────── */

static void ota_report(const char *state, const char *id, const char *detail)
{
    if (s_cfg.publish == NULL || s_cfg.status_topic == NULL || s_cfg.status_topic[0] == '\0') {
        return;
    }
    const esp_app_desc_t *d = esp_app_get_description();
    char msg[320];
    int n = snprintf(msg, sizeof msg,
        "{\"type\":\"ota_status\",\"device_id\":\"%s\",\"id\":\"%s\",\"state\":\"%s\","
        "\"fw\":\"%.32s\"%s%s%s}",
        s_cfg.device_id ? s_cfg.device_id : "", id ? id : "", state,
        d ? d->version : "",
        detail ? ",\"detail\":\"" : "", detail ? detail : "", detail ? "\"" : "");
    if (n > 0 && (size_t)n < sizeof msg) {
        int msg_id = 0;
        s_cfg.publish(s_cfg.status_topic, msg, (size_t)n, &msg_id);
    }
}

/* Poll until MQTT is connected or the timeout elapses. */
static bool wait_connected(uint32_t timeout_s)
{
    if (s_cfg.is_connected == NULL) return false;
    uint32_t waited = 0;
    while (waited < timeout_s * 1000U) {
        if (s_cfg.is_connected()) return true;
        vTaskDelay(pdMS_TO_TICKS(OTA_CONNECT_POLL_MS));
        waited += OTA_CONNECT_POLL_MS;
    }
    return s_cfg.is_connected();
}

/* ── NVS pending-id latch ──────────────────────────────────────────────── */

static void latch_set(const char *id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, KEY_PENDING, id ? id : "");
    nvs_commit(h);
    nvs_close(h);
}

static void latch_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, KEY_PENDING);
    nvs_commit(h);
    nvs_close(h);
}

static bool latch_get(char *out, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, KEY_PENDING, out, &len);
    nvs_close(h);
    return err == ESP_OK;
}

/* ── one OTA download ──────────────────────────────────────────────────── */

static void ota_do_update(const ota_request_t *r)
{
    ESP_LOGW(TAG, "OTA requested id=%s url=%s", r->id, r->url);
    ota_report("accepted", r->id, NULL);
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the accepted report flush before comms drop */

    /* Latch the id so the post-reboot confirm path can report it. */
    latch_set(r->id);

    /* Free MQTT's TLS heap — the board can't hold two TLS sessions at once. */
    if (s_cfg.comms_suspend != NULL) s_cfg.comms_suspend();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Proven Stage-0 settings: cert bundle validates GitHub + its CDN across the
     * 302 redirect; 4 KiB HTTP buffers fit GitHub's long signed redirect URL. */
    esp_http_client_config_t http = {
        .url               = r->url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 20000,
        .keep_alive_enable = true,
        .buffer_size       = 4096,
        .buffer_size_tx    = 4096,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    /* Simple API: begin → perform → end → set_boot_partition (all-in-one). With
     * rollback enabled the new image boots PENDING_VERIFY. */
    esp_err_t err = esp_https_ota(&cfg);

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "OTA image written + boot set — rebooting into the new image");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();   /* no return */
    }

    /* Failed: not booting the new image — clear the latch, bring comms back, report. */
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    latch_clear();
    if (s_cfg.comms_resume != NULL) s_cfg.comms_resume();
    if (wait_connected(60)) {
        ota_report("failed", r->id, esp_err_to_name(err));
    }
}

/* ── post-reboot confirmation of a just-applied image ──────────────────── */

static void confirm_pending_after_boot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run == NULL || esp_ota_get_state_partition(run, &st) != ESP_OK ||
        st != ESP_OTA_IMG_PENDING_VERIFY) {
        return;   /* normal boot — nothing to confirm */
    }

    char id[OTA_ID_MAX] = "";
    (void)latch_get(id, sizeof id);
    ESP_LOGW(TAG, "booted a PENDING_VERIFY image (id=%s) — confirming via MQTT", id);

    if (wait_connected(OTA_CONFIRM_TIMEOUT_S)) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGW(TAG, "image confirmed valid: %s", esp_err_to_name(err));
        latch_clear();
        ota_report("success", id, NULL);
    } else {
        /* Couldn't prove health within the window — revert to the known-good slot. */
        ESP_LOGE(TAG, "no MQTT within %ds — rolling back", OTA_CONFIRM_TIMEOUT_S);
        latch_clear();
        esp_ota_mark_app_invalid_rollback_and_reboot();   /* no return on success */
    }
}

static void ota_task(void *arg)
{
    (void)arg;
    confirm_pending_after_boot();   /* handle a just-applied image before serving requests */

    ota_request_t r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) == pdTRUE) {
            ota_do_update(&r);
        }
    }
}

/* ── public API ────────────────────────────────────────────────────────── */

esp_err_t ota_update_init(const ota_update_config_t *cfg)
{
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (s_task != NULL) return ESP_OK;   /* idempotent */
    s_cfg = *cfg;

    s_queue = xQueueCreate(2, sizeof(ota_request_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;

    if (xTaskCreate(ota_task, "ota_update", OTA_TASK_STACK, NULL,
                    OTA_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTA worker started");
    return ESP_OK;
}

esp_err_t ota_update_request(const char *url, const char *id)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (url == NULL || url[0] == '\0' || strlen(url) >= OTA_URL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_request_t r;
    memset(&r, 0, sizeof r);
    strncpy(r.url, url, sizeof r.url - 1);
    if (id != NULL) strncpy(r.id, id, sizeof r.id - 1);
    return (xQueueSend(s_queue, &r, 0) == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}
