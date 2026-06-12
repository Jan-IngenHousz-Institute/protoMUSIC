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
#include "freertos/task.h"
#include "sd_card.h"
#include "uart_sensor_port.h"

#define TAG "ambit_ota"

#define AMBIT_OTA_TASK_STACK   8192
#define AMBIT_OTA_TASK_PRIO    4          /* below lua_runner(10); not latency-critical */
#define AMBIT_OTA_URL_MAX      256
#define AMBIT_OTA_MAX_RETRY    4          /* per-chunk resend attempts on CRC/transport error */
#define AMBIT_OTA_REBOOT_WAIT_MS 5000     /* let the C3 reboot into the new image before re-querying */
#define AMBIT_OTA_DL_BUF       4096
#define AMBIT_FW_PATH          "/sdcard/ambit_fw.bin"

typedef struct {
    uint8_t channel;
    char    url[AMBIT_OTA_URL_MAX];
} ambit_ota_req_t;

static ambit_ota_config_t s_cfg;
static QueueHandle_t      s_queue;
static TaskHandle_t       s_task;

/* ── best-effort status report (console always; MQTT if connected) ───────── */

static void report(const char *state, uint8_t ch)
{
    if (s_cfg.publish == NULL || s_cfg.status_topic == NULL || s_cfg.status_topic[0] == '\0') {
        return;
    }
    if (s_cfg.is_connected != NULL && !s_cfg.is_connected()) {
        return;   /* MQTT not back yet after resume — console log already covered it */
    }
    char msg[200];
    int n = snprintf(msg, sizeof msg,
        "{\"type\":\"ambit_ota_status\",\"device_id\":\"%s\",\"channel\":%u,\"state\":\"%s\"}",
        s_cfg.device_id ? s_cfg.device_id : "", (unsigned)ch, state);
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

static void ambit_log_version(uint8_t ch, const char *when)
{
    uint8_t buf[64];
    size_t  len = 0;
    cmd_result_t r = cmd_ambit_get_info(ch, AMBIT_INFO_FW, buf, sizeof buf, &len);
    if (r.status == ESP_OK && len >= sizeof(ambit_fw_info_t)) {
        const ambit_fw_info_t *fw = (const ambit_fw_info_t *)buf;
        ESP_LOGW(TAG, "AMBIT%u fw %s: v%u.%u.%u (size=%lu)", ch + 1, when,
                 fw->major, fw->minor, fw->batch, (unsigned long)fw->size);
    } else {
        ESP_LOGW(TAG, "AMBIT%u fw %s: no answer (%s)", ch + 1, when, esp_err_to_name(r.status));
    }
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

/* ── one AMBIT OTA ────────────────────────────────────────────────────────── */

static void ambit_do_ota(uint8_t ch, const char *url)
{
    ESP_LOGW(TAG, "AMBIT OTA requested: ch=%u url=%s", ch, url);

    if (strstr(url, "/blob/") != NULL || strstr(url, "/tree/") != NULL) {
        ESP_LOGE(TAG, "URL is a GitHub web page (/blob/ or /tree/), not a file:");
        ESP_LOGE(TAG, "  %s", url);
        ESP_LOGE(TAG, "use a direct .bin — raw.githubusercontent.com/<owner>/<repo>/<branch>/<path>");
        report("failed", ch);
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
        ambit_log_version(ch, "before");

        size_t    img_size = 0;
        esp_err_t err = http_get_to_file(url, AMBIT_FW_PATH, &img_size);
        if (err != ESP_OK || img_size == 0) {
            ESP_LOGE(TAG, "download failed (%s, %u bytes)", esp_err_to_name(err), (unsigned)img_size);
        } else {
            ESP_LOGW(TAG, "downloaded %u bytes -> %s", (unsigned)img_size, AMBIT_FW_PATH);
            FILE *f = fopen(AMBIT_FW_PATH, "rb");
            if (f == NULL) {
                ESP_LOGE(TAG, "cannot reopen %s for read", AMBIT_FW_PATH);
            } else {
                ok = ambit_stream_image(ch, f, img_size);
                fclose(f);
                if (ok) {
                    ESP_LOGW(TAG, "OTA_END ok — AMBIT%u rebooting; waiting %d ms then re-checking",
                             ch + 1, AMBIT_OTA_REBOOT_WAIT_MS);
                    vTaskDelay(pdMS_TO_TICKS(AMBIT_OTA_REBOOT_WAIT_MS));
                    ambit_log_version(ch, "after ");
                }
            }
        }
    }

    if (s_cfg.comms_resume != NULL) s_cfg.comms_resume();
    if (s_cfg.workload_resume != NULL) s_cfg.workload_resume();

    if (ok) {
        ESP_LOGW(TAG, "AMBIT%u OTA SUCCESS", ch + 1);
        report("success", ch);
    } else {
        ESP_LOGE(TAG, "AMBIT%u OTA FAILED — sensor kept its current image", ch + 1);
        report("failed", ch);
    }
}

static void ambit_ota_task(void *arg)
{
    (void)arg;
    ambit_ota_req_t r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) == pdTRUE) {
            ambit_do_ota(r.channel, r.url);
        }
    }
}

/* ── public API ───────────────────────────────────────────────────────────── */

esp_err_t ambit_ota_init(const ambit_ota_config_t *cfg)
{
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (s_task != NULL) return ESP_OK;   /* idempotent */
    s_cfg = *cfg;

    s_queue = xQueueCreate(2, sizeof(ambit_ota_req_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;

    /* Core 0 like ota_update: lua_runner (the UART's other user) is on core 1
     * and suspended during the update anyway; the download yields on I/O. */
    if (xTaskCreatePinnedToCore(ambit_ota_task, "ambit_ota", AMBIT_OTA_TASK_STACK, NULL,
                                AMBIT_OTA_TASK_PRIO, &s_task, 0) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "AMBIT OTA worker started");
    return ESP_OK;
}

esp_err_t ambit_ota_request(uint8_t channel, const char *url)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (channel >= UART_SENSOR_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;
    if (url == NULL || url[0] == '\0' || strlen(url) >= AMBIT_OTA_URL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    ambit_ota_req_t r;
    memset(&r, 0, sizeof r);
    r.channel = channel;
    strncpy(r.url, url, sizeof r.url - 1);
    if (xQueueSend(s_queue, &r, 0) == pdTRUE) return ESP_OK;
    return ESP_ERR_NO_MEM;   /* an update is already queued/in-flight */
}
