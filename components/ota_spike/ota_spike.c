#include "ota_spike.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "OTA_SPIKE"

/* Log a heap snapshot every this many bytes downloaded. */
#define OTA_SPIKE_LOG_STRIDE (64 * 1024)

static inline size_t internal_largest_block(void)
{
    /* Internal DRAM only — TLS + the Wi-Fi driver allocate from here (no PSRAM),
     * and this is the number that collapses first under fragmentation. */
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

int ota_spike_run(const char *url)
{
    const size_t free0 = esp_get_free_heap_size();
    const size_t lblk0 = internal_largest_block();
    ESP_LOGW(TAG, "==== OTA SPIKE START ====");
    ESP_LOGW(TAG, "url=%s", url);
    ESP_LOGW(TAG, "before begin: free=%u largest_internal=%u", (unsigned)free0, (unsigned)lblk0);

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* validates github.com AND the CDN host across the 302 */
        .timeout_ms        = 20000,
        .keep_alive_enable = true,
        /* The 512 B defaults are too small for GitHub: the 302 redirects to a very
         * long signed CDN URL (X-Amz-* query params) that overflows the TX buffer,
         * and Fastly/S3 return verbose response headers that overflow the RX buffer
         * -> "HTTP_CLIENT: Out of buffer". Heap is plentiful here, so size up. */
        .buffer_size       = 4096,   /* RX: response headers + data reads */
        .buffer_size_tx    = 4096,   /* TX: request line incl. the long redirected URL */
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    /* ── 1. begin: this is the TLS-handshake gate ──────────────────────── */
    esp_https_ota_handle_t handle = NULL;
    const int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "*** BEGIN FAILED: %s ***", esp_err_to_name(err));
        ESP_LOGE(TAG, "    free=%u largest_internal=%u — diagnose by the lines ABOVE: "
                      "'Certificate validated' = TLS handshake OK (so it is NOT the 8 KiB "
                      "record buffer); 'HTTP_CLIENT: Out of buffer' = HTTP buffers too small "
                      "for GitHub's redirect (raise buffer_size/buffer_size_tx); a TLS/esp-tls "
                      "error with heap still high = raise MBEDTLS_SSL_IN_CONTENT_LEN.",
                 (unsigned)esp_get_free_heap_size(), (unsigned)internal_largest_block());
        ESP_LOGW(TAG, "==== OTA SPIKE END (handshake failed) ====");
        return err;
    }
    ESP_LOGW(TAG, ">>> HANDSHAKE OK — TLS + redirect + image header fit in 8 KiB. "
                  "free=%u largest_internal=%u",
             (unsigned)esp_get_free_heap_size(), (unsigned)internal_largest_block());

    esp_app_desc_t desc;
    if (esp_https_ota_get_img_desc(handle, &desc) == ESP_OK) {
        ESP_LOGW(TAG, "    served image: project=%.32s version=%.32s idf=%.32s",
                 desc.project_name, desc.version, desc.idf_ver);
    } else {
        ESP_LOGW(TAG, "    (could not read image descriptor — URL may not be a valid app image)");
    }

    /* ── 2. perform loop: streaming download + heap low-water ───────────── */
    size_t min_lblk = lblk0;
    int    last_logged = 0;
    do {
        err = esp_https_ota_perform(handle);
        const size_t lblk = internal_largest_block();
        if (lblk < min_lblk) {
            min_lblk = lblk;
        }
        const int got = esp_https_ota_get_image_len_read(handle);
        if (got - last_logged >= OTA_SPIKE_LOG_STRIDE) {
            last_logged = got;
            ESP_LOGI(TAG, "downloaded=%d free=%u largest_internal=%u",
                     got, (unsigned)esp_get_free_heap_size(), (unsigned)lblk);
        }
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    const int     total    = esp_https_ota_get_image_len_read(handle);
    const bool    complete = esp_https_ota_is_complete_data_received(handle);
    const int64_t dt_ms     = (esp_timer_get_time() - t0) / 1000;

    /* ── 3. ABORT — free everything, never set the boot partition ───────── */
    esp_https_ota_abort(handle);

    ESP_LOGW(TAG, "==== OTA SPIKE RESULT ====");
    ESP_LOGW(TAG, "perform_status = %s", esp_err_to_name(err));
    ESP_LOGW(TAG, "complete_image = %s", complete ? "YES" : "NO (partial)");
    ESP_LOGW(TAG, "bytes          = %d", total);
    ESP_LOGW(TAG, "elapsed_ms     = %lld  (~%lld KiB/s)",
             (long long)dt_ms, dt_ms > 0 ? (long long)total / dt_ms : 0);
    ESP_LOGW(TAG, "heap floor     = largest_internal min %u bytes during download",
             (unsigned)min_lblk);
    ESP_LOGW(TAG, "VERDICT: handshake=OK  download=%s  heap_floor=%u "
                  "(needs to stay well above ~6.5 KiB to coexist with publishing)",
             complete ? "OK" : "INCOMPLETE", (unsigned)min_lblk);
    ESP_LOGW(TAG, "==== OTA SPIKE END (boot partition UNCHANGED) ====");
    return (int)err;
}
