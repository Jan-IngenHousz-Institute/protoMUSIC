#include "ambit_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ambit_protocol.h"
#include "device_commands.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sd_card.h"
#include "uart_sensor_port.h"

#define TAG "ambit_ota"

#define AMBIT_OTA_TASK_STACK   8192
#define AMBIT_OTA_TASK_PRIO    4          /* below lua_runner(10); not latency-critical */
#define AMBIT_OTA_URL_MAX      256
#define AMBIT_OTA_MAX_RETRY    4          /* per-chunk resend attempts on CRC/transport error */
#define AMBIT_OTA_REBOOT_WAIT_MS 5000     /* let the C3 reboot into the new image before re-querying */
#define AMBIT_OTA_DL_BUF       4096
#define AMBIT_OTA_ID_MAX       64
#define AMBIT_OTA_IDLE_EXIT_MS 2000   /* free the 8 KB worker stack this long after idle */
#define AMBIT_FW_PATH          "/sdcard/ambit_fw.bin"

#define NVS_NS                 "ambit_ota"
#define KEY_APPLIED            "applied_id"   /* id of the last *successfully* applied update */

#define AMBIT_OP_OTA       0
#define AMBIT_OP_VERSIONS  1

typedef struct {
    uint8_t op;                       /* AMBIT_OP_OTA | AMBIT_OP_VERSIONS */
    uint8_t channel;
    char    url[AMBIT_OTA_URL_MAX];
    char    id[AMBIT_OTA_ID_MAX];
} ambit_ota_req_t;

static ambit_ota_config_t s_cfg;
static QueueHandle_t      s_queue;
static TaskHandle_t       s_task;     /* NULL when no worker is running (lazy) */
static SemaphoreHandle_t  s_lock;     /* guards s_task lifecycle vs enqueue */

/* ── dedupe latch (NVS) ──────────────────────────────────────────────────
 * Mirror of ota_update: an id is latched only on a *successful* update, so a
 * retained/duplicate MQTT trigger for an already-applied id is ignored, while a
 * failed attempt stays retryable under the same id. CLI passes a NULL id (an
 * operator-initiated update is never deduped). */
static void latch_set(const char *id)
{
    if (id == NULL || id[0] == '\0') return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, KEY_APPLIED, id) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static bool already_applied(const char *id)
{
    if (id == NULL || id[0] == '\0') return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    char prev[AMBIT_OTA_ID_MAX] = "";
    size_t len = sizeof prev;
    esp_err_t err = nvs_get_str(h, KEY_APPLIED, prev, &len);
    nvs_close(h);
    return err == ESP_OK && strcmp(prev, id) == 0;
}

/* ── best-effort status report (console always; MQTT if connected) ───────── */

/* Minimal JSON string escape for the attacker-influenced id in status reports. */
static void json_escape(char *out, size_t cap, const char *in)
{
    size_t o = 0;
    if (in == NULL) { if (cap) out[0] = '\0'; return; }
    for (const char *p = in; *p != '\0' && o + 2 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c >= 0x20)        { out[o++] = (char)c; }
    }
    out[o] = '\0';
}

static void report(const char *state, uint8_t ch, const char *id)
{
    if (s_cfg.publish == NULL || s_cfg.status_topic == NULL || s_cfg.status_topic[0] == '\0') {
        return;
    }
    if (s_cfg.is_connected != NULL && !s_cfg.is_connected()) {
        return;   /* MQTT not back yet after resume — console log already covered it */
    }
    char esc_id[AMBIT_OTA_ID_MAX * 2 + 1] = "";
    json_escape(esc_id, sizeof esc_id, id);
    char msg[224];
    int n = snprintf(msg, sizeof msg,
        "{\"type\":\"ambit_ota_status\",\"device_id\":\"%s\",\"id\":\"%s\",\"channel\":%u,\"state\":\"%s\"}",
        s_cfg.device_id ? s_cfg.device_id : "", esc_id, (unsigned)ch, state);
    if (n > 0 && (size_t)n < sizeof msg) {
        int msg_id = 0;
        s_cfg.publish(s_cfg.status_topic, msg, (size_t)n, &msg_id);
    }
}

/* ── HTTP GET → file on SD ────────────────────────────────────────────────
 * Streaming download (the image is too big to buffer in RAM). Requires a
 * direct-200 URL (raw.githubusercontent.com/… or a pre-resolved link); a 3xx
 * redirect is reported as an error rather than silently saving an HTML page
 * (release-asset 302 following is a follow-up). Same proven TLS/buffer settings
 * as the ambyte self-OTA (cert bundle validates GitHub + its CDN; 4 KiB buffers
 * fit GitHub's long signed-redirect URLs). */
static esp_err_t http_get_to_file(const char *url, const char *path, size_t *out_size)
{
    *out_size = 0;

    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 20000,
        .keep_alive_enable = true,
        .buffer_size       = 4096,
        .buffer_size_tx    = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == NULL) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(c);
        return err;
    }

    int64_t clen   = esp_http_client_fetch_headers(c);
    int     status = esp_http_client_get_status_code(c);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status %d (expected 200)%s", status,
                 (status >= 300 && status < 400)
                     ? " — redirect; use a direct raw.githubusercontent.com URL" : "");
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s for write", path);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ESP_FAIL;
    }
    uint8_t *buf = malloc(AMBIT_OTA_DL_BUF);
    if (buf == NULL) {
        fclose(f);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (1) {
        int r = esp_http_client_read(c, (char *)buf, AMBIT_OTA_DL_BUF);
        if (r < 0) { err = ESP_FAIL; break; }
        if (r == 0) break;   /* EOF (content-length reached or stream closed) */
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) { err = ESP_ERR_NO_MEM; break; }
        total += (size_t)r;
        vTaskDelay(1);       /* yield so the idle task is fed on a fast link */
    }

    free(buf);
    fclose(f);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (err == ESP_OK && clen > 0 && total != (size_t)clen) {
        ESP_LOGE(TAG, "short download: %u of %lld bytes", (unsigned)total, (long long)clen);
        err = ESP_FAIL;
    }
    if (err == ESP_OK) *out_size = total;
    return err;
}

/* ── version read (cmd 33/2) — pre/post-OTA confirmation ─────────────────── */

/* Log the AMBIT's reported FW version. Returns true if it answered (image alive). */
static bool ambit_log_version(uint8_t ch, const char *when)
{
    uint8_t buf[64];
    size_t  len = 0;
    cmd_result_t r = cmd_ambit_get_info(ch, AMBIT_INFO_FW, buf, sizeof buf, &len);
    if (r.status == ESP_OK && len >= sizeof(ambit_fw_info_t)) {
        const ambit_fw_info_t *fw = (const ambit_fw_info_t *)buf;
        ESP_LOGW(TAG, "AMBIT%u fw %s: v%u.%u.%u (size=%lu)", ch + 1, when,
                 fw->major, fw->minor, fw->batch, (unsigned long)fw->size);
        return true;
    }
    ESP_LOGW(TAG, "AMBIT%u fw %s: no answer (%s)", ch + 1, when, esp_err_to_name(r.status));
    return false;
}

/* ── stream a staged image to one AMBIT ──────────────────────────────────── */

static bool ambit_stream_image(uint8_t ch, FILE *f, size_t img_size)
{
    uint8_t status = 0xFF;

    cmd_result_t r = cmd_ambit_ota_begin(ch, (uint32_t)img_size, &status);
    if (r.status != ESP_OK || status != 0) {
        ESP_LOGE(TAG, "OTA_BEGIN failed (%s, status=%u)", esp_err_to_name(r.status), status);
        return false;
    }

    uint8_t  buf[AMBIT_OTA_CHUNK_MAX];
    uint16_t seq = 0;
    size_t   sent = 0;
    int      last_decile = -1;
    size_t   n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        bool chunk_ok = false;
        for (int tries = 0; tries < AMBIT_OTA_MAX_RETRY && !chunk_ok; tries++) {
            r = cmd_ambit_ota_data(ch, seq, buf, (uint8_t)n, &status);
            if (r.status == ESP_OK && status == 0) {
                chunk_ok = true;
            } else {
                ESP_LOGW(TAG, "chunk seq=%u try=%d: err=%s status=%u",
                         seq, tries + 1, esp_err_to_name(r.status), status);
            }
        }
        if (!chunk_ok) {
            ESP_LOGE(TAG, "chunk seq=%u failed after %d tries (status=%u) — aborting",
                     seq, AMBIT_OTA_MAX_RETRY, status);
            cmd_ambit_ota_abort(ch, &status);
            return false;
        }
        sent += n;
        seq++;
        int decile = (img_size > 0) ? (int)(sent * 10 / img_size) : 0;
        if (decile != last_decile) {
            ESP_LOGW(TAG, "  streaming %d%% (%u/%u B, %u chunks)",
                     decile * 10, (unsigned)sent, (unsigned)img_size, (unsigned)seq);
            last_decile = decile;
        }
    }

    r = cmd_ambit_ota_end(ch, &status);
    if (r.status != ESP_OK || status != 0) {
        ESP_LOGE(TAG, "OTA_END failed (%s, status=%u) — AMBIT kept its old image",
                 esp_err_to_name(r.status), status);
        return false;
    }
    return true;
}

/* ── one channel: stream the staged image + confirm after reboot ──────────── */

/* Stream AMBIT_FW_PATH (img_size bytes) to channel `ch`, then — after the C3
 * reboots into the new (PENDING_VERIFY) image — confirm it ONLY if it answers.
 * If it doesn't answer or the confirm fails, the C3 bootloader rolls back to the
 * previous image on its next reboot. Returns true only when confirmed healthy. */
static bool ambit_ota_one(uint8_t ch, size_t img_size)
{
    ambit_log_version(ch, "before");

    FILE *f = fopen(AMBIT_FW_PATH, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot reopen %s for read", AMBIT_FW_PATH);
        return false;
    }
    bool streamed = ambit_stream_image(ch, f, img_size);
    fclose(f);
    if (!streamed) return false;

    ESP_LOGW(TAG, "OTA_END ok — AMBIT%u rebooting; waiting %d ms then re-checking",
             ch + 1, AMBIT_OTA_REBOOT_WAIT_MS);
    vTaskDelay(pdMS_TO_TICKS(AMBIT_OTA_REBOOT_WAIT_MS));

    /* Retry the post-reboot read: a slow boot or one dropped reply must NOT be
     * mistaken for a dead image (that would skip confirm and force a needless
     * rollback on the C3's next reboot). */
    bool alive = false;
    for (int tries = 0; tries < 3 && !alive; tries++) {
        if (tries) vTaskDelay(pdMS_TO_TICKS(1000));
        alive = ambit_log_version(ch, "after ");
    }
    if (!alive) {
        ESP_LOGE(TAG, "AMBIT%u not answering after OTA — NOT confirming; it will roll "
                      "back to the previous image on its next reboot", ch + 1);
        return false;
    }
    uint8_t st = 0xFF;
    for (int tries = 0; tries < 3; tries++) {
        cmd_result_t cr = cmd_ambit_ota_confirm(ch, &st);
        if (cr.status == ESP_OK && st == 0) {
            ESP_LOGW(TAG, "AMBIT%u image confirmed — rollback cancelled", ch + 1);
            return true;
        }
        ESP_LOGW(TAG, "OTA_CONFIRM try %d: %s st=%u", tries + 1, esp_err_to_name(cr.status), st);
    }
    ESP_LOGE(TAG, "AMBIT%u confirm failed — image will roll back on its next reboot", ch + 1);
    return false;
}

/* ── one OTA request (single channel, or a sweep of all present channels) ──── */

static void ambit_do_ota(const ambit_ota_req_t *r)
{
    const char *url = r->url;
    const bool  all = (r->channel == AMBIT_OTA_CH_ALL);
    if (all) {
        ESP_LOGW(TAG, "AMBIT OTA requested: ch=all id=%s url=%s", r->id[0] ? r->id : "(none)", url);
    } else {
        ESP_LOGW(TAG, "AMBIT OTA requested: ch=%u id=%s url=%s",
                 r->channel, r->id[0] ? r->id : "(none)", url);
    }

    if (strstr(url, "/blob/") != NULL || strstr(url, "/tree/") != NULL) {
        ESP_LOGE(TAG, "URL is a GitHub web page (/blob/ or /tree/), not a file:");
        ESP_LOGE(TAG, "  %s", url);
        ESP_LOGE(TAG, "use a direct .bin — raw.githubusercontent.com/<owner>/<repo>/<branch>/<path>");
        report("failed", r->channel, r->id);
        return;
    }

    /* Quiesce: free the UART (stop Lua) and the heap/TLS (stop MQTT). */
    if (s_cfg.workload_suspend != NULL) s_cfg.workload_suspend();
    if (s_cfg.comms_suspend != NULL) s_cfg.comms_suspend();
    vTaskDelay(pdMS_TO_TICKS(500));

    bool ok = false;
    if (!sdcard_is_mounted() && sdcard_mount() != ESP_OK) {
        ESP_LOGE(TAG, "SD not available — cannot stage the AMBIT image");
    } else {
        size_t    img_size = 0;
        esp_err_t err = http_get_to_file(url, AMBIT_FW_PATH, &img_size);   /* download once */
        if (err != ESP_OK || img_size == 0) {
            ESP_LOGE(TAG, "download failed (%s, %u bytes)", esp_err_to_name(err), (unsigned)img_size);
        } else {
            ESP_LOGW(TAG, "downloaded %u bytes -> %s", (unsigned)img_size, AMBIT_FW_PATH);
            if (all) {
                /* Sweep every present channel, sequentially (the UART is shared). */
                int present = 0, ok_count = 0;
                for (uint8_t c = 0; c < UART_SENSOR_NUM_CHANNELS; c++) {
                    bool connected = false;
                    cmd_result_t pr = cmd_uart_ping(c, &connected);
                    if (pr.status != ESP_OK || !connected) {
                        ESP_LOGW(TAG, "AMBIT%u: not present — skipping", c + 1);
                        continue;
                    }
                    present++;
                    bool ok_c = ambit_ota_one(c, img_size);
                    report(ok_c ? "success" : "failed", c, r->id);
                    if (ok_c) ok_count++;
                }
                ESP_LOGW(TAG, "AMBIT OTA all: %d/%d present channels updated", ok_count, present);
                ok = (present > 0 && ok_count == present);
            } else {
                ok = ambit_ota_one(r->channel, img_size);
            }
        }
    }

    if (s_cfg.comms_resume != NULL) s_cfg.comms_resume();
    if (s_cfg.workload_resume != NULL) s_cfg.workload_resume();

    if (ok) {
        latch_set(r->id);   /* dedupe a retained trigger — only when fully successful */
        ESP_LOGW(TAG, "AMBIT OTA SUCCESS (id=%s)", r->id[0] ? r->id : "(none)");
        report("success", r->channel, r->id);
    } else {
        ESP_LOGE(TAG, "AMBIT OTA FAILED (id=%s)", r->id[0] ? r->id : "(none)");
        report("failed", r->channel, r->id);
    }
}

/* ── fleet version report ─────────────────────────────────────────────────
 * Sweep all channels (cmd 33/2), log a per-channel line, and publish one JSON
 * report. Runs on the worker task (off the MQTT loop). No quiesce — the version
 * read is a quick UART transaction the uart_sensors mutex serializes with Lua. */
static void ambit_do_versions(const char *id)
{
    char esc_id[AMBIT_OTA_ID_MAX * 2 + 1] = "";
    json_escape(esc_id, sizeof esc_id, id);

    char buf[512];
    int  o = snprintf(buf, sizeof buf,
        "{\"type\":\"ambit_versions\",\"device_id\":\"%s\",\"id\":\"%s\",\"channels\":[",
        s_cfg.device_id ? s_cfg.device_id : "", esc_id);

    for (uint8_t c = 0; c < UART_SENSOR_NUM_CHANNELS; c++) {
        if (o < 0 || o >= (int)sizeof buf - 48) break;   /* leave room for one entry + "]}" */
        const char *sep = (c == 0) ? "" : ",";
        bool connected = false;
        cmd_result_t pr = cmd_uart_ping(c, &connected);
        if (pr.status == ESP_OK && connected) {
            uint8_t vb[64];
            size_t  len = 0;
            cmd_result_t r = cmd_ambit_get_info(c, AMBIT_INFO_FW, vb, sizeof vb, &len);
            if (r.status == ESP_OK && len >= sizeof(ambit_fw_info_t)) {
                const ambit_fw_info_t *fw = (const ambit_fw_info_t *)vb;
                ESP_LOGW(TAG, "AMBIT%u: v%u.%u.%u", c + 1, fw->major, fw->minor, fw->batch);
                o += snprintf(buf + o, sizeof buf - o,
                    "%s{\"ch\":%u,\"present\":true,\"version\":\"%u.%u.%u\"}",
                    sep, c, fw->major, fw->minor, fw->batch);
            } else {
                ESP_LOGW(TAG, "AMBIT%u: present, no version", c + 1);
                o += snprintf(buf + o, sizeof buf - o, "%s{\"ch\":%u,\"present\":true}", sep, c);
            }
        } else {
            ESP_LOGW(TAG, "AMBIT%u: absent", c + 1);
            o += snprintf(buf + o, sizeof buf - o, "%s{\"ch\":%u,\"present\":false}", sep, c);
        }
    }

    if (o > 0 && o < (int)sizeof buf - 2) {
        o += snprintf(buf + o, sizeof buf - o, "]}");
        if (s_cfg.publish != NULL && s_cfg.status_topic != NULL && s_cfg.status_topic[0] != '\0' &&
            (s_cfg.is_connected == NULL || s_cfg.is_connected())) {
            int msg_id = 0;
            s_cfg.publish(s_cfg.status_topic, buf, (size_t)o, &msg_id);
        }
    }
}

/* Lazy worker: spawned on demand by ambit_ota_enqueue(), exits (freeing its 8 KB
 * stack) once idle. This board runs near its heap limit (~17 KB largest block, no
 * PSRAM); a permanently-resident 8 KB stack for a rare maintenance op would shrink
 * the headroom the telemetry drain needs to buffer a ~4 KB AMBIT payload. The exit
 * decision and the enqueue+spawn both run under s_lock, and the task re-checks the
 * queue under the lock before deleting, so a request can't race into a dying task. */
static void ambit_ota_task(void *arg)
{
    (void)arg;
    ambit_ota_req_t r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, pdMS_TO_TICKS(AMBIT_OTA_IDLE_EXIT_MS)) == pdTRUE) {
            if (r.op == AMBIT_OP_VERSIONS) {
                ambit_do_versions(r.id);
            } else {
                ambit_do_ota(&r);
            }
            continue;
        }
        /* Idle timeout: exit to give the stack back, unless a request just arrived. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (uxQueueMessagesWaiting(s_queue) == 0) {
            s_task = NULL;
            xSemaphoreGive(s_lock);
            vTaskDelete(NULL);   /* no return — stack reclaimed */
        }
        xSemaphoreGive(s_lock);   /* a request raced in; loop and process it */
    }
}

/* Queue a request, spawning the worker if it isn't running. */
static esp_err_t ambit_ota_enqueue(const ambit_ota_req_t *r)
{
    if (s_queue == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (xQueueSend(s_queue, r, 0) != pdTRUE) {
        ret = ESP_ERR_NO_MEM;                 /* an op is already queued/in-flight */
    } else if (s_task == NULL) {
        /* Core 0 like ota_update: lua_runner (the UART's other user) is on core 1
         * and suspended during the op anyway; the download yields on I/O. */
        if (xTaskCreatePinnedToCore(ambit_ota_task, "ambit_ota", AMBIT_OTA_TASK_STACK,
                                    NULL, AMBIT_OTA_TASK_PRIO, &s_task, 0) != pdPASS) {
            ambit_ota_req_t drop;
            xQueueReceive(s_queue, &drop, 0);  /* undo the enqueue — no worker to run it */
            s_task = NULL;
            ret = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}

/* ── public API ───────────────────────────────────────────────────────────── */

esp_err_t ambit_ota_init(const ambit_ota_config_t *cfg)
{
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    if (s_queue != NULL) return ESP_OK;   /* idempotent */

    s_lock  = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(2, sizeof(ambit_ota_req_t));
    if (s_lock == NULL || s_queue == NULL) {
        if (s_lock)  vSemaphoreDelete(s_lock);
        if (s_queue) vQueueDelete(s_queue);
        s_lock = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    /* No task yet — spawned on the first request, exits when idle (lazy). */
    ESP_LOGI(TAG, "AMBIT OTA ready (worker spawned on demand)");
    return ESP_OK;
}

esp_err_t ambit_ota_request(uint8_t channel, const char *url, const char *id)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (channel >= UART_SENSOR_NUM_CHANNELS && channel != AMBIT_OTA_CH_ALL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (url == NULL || url[0] == '\0' || strlen(url) >= AMBIT_OTA_URL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Dedupe on the last successfully-applied id (idempotent under a retained
     * MQTT trigger). A NULL id (CLI) or a failed attempt is never deduped. */
    if (already_applied(id)) {
        ESP_LOGI(TAG, "ambit_ota id=%s already applied — ignoring", id);
        return ESP_OK;
    }
    ambit_ota_req_t r;
    memset(&r, 0, sizeof r);
    r.op      = AMBIT_OP_OTA;
    r.channel = channel;
    strncpy(r.url, url, sizeof r.url - 1);
    if (id != NULL) strncpy(r.id, id, sizeof r.id - 1);
    return ambit_ota_enqueue(&r);
}

esp_err_t ambit_ota_report_versions(const char *id)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    ambit_ota_req_t r;
    memset(&r, 0, sizeof r);
    r.op = AMBIT_OP_VERSIONS;
    if (id != NULL) strncpy(r.id, id, sizeof r.id - 1);
    return ambit_ota_enqueue(&r);
}
