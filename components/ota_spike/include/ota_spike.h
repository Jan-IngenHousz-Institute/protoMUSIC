#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Stage-0 OTA spike (docs/ota-update-plan.md): answer the ONE gating question
 * before any OTA work — does an HTTPS firmware download from GitHub actually
 * work on THIS board, given the TLS record buffer is pinned at 8 KiB
 * (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192) and internal RAM is tight (no PSRAM,
 * largest free block ~17 KiB)?
 *
 * It uses the REAL esp_https_ota advanced API against `url`, so it exercises the
 * exact path the feature will use:
 *   1. TLS handshake to github.com + the cross-host redirect to the Fastly CDN
 *      (objects.githubusercontent.com) — validated via the certificate bundle.
 *      If GitHub's handshake cert record does not fit in 8 KiB, esp_https_ota_begin
 *      fails here, with plenty of free heap — a definitive "buffer too small",
 *      not a heap problem.
 *   2. The full streaming download, logging the internal-RAM largest-free-block
 *      low-water mark throughout (the number that must clear ~6.5 KiB for the
 *      normal publish path).
 *
 * SAFETY: it downloads + writes into the (currently unused) ota_0 partition but
 * calls esp_https_ota_abort() — it NEVER calls esp_https_ota_finish() and NEVER
 * sets the boot partition, so it cannot change what the device boots. (The board
 * also has no otadata partition yet, so a boot switch is impossible regardless.)
 *
 * Build-flag gated (SPIKE_OTA). Requires the cert bundle
 * (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y, see sdkconfig.defaults) and Wi-Fi up.
 * Call it after Wi-Fi connects; it does not return control to normal startup.
 *
 * Returns the final esp_https_ota_perform() status (ESP_OK = full image received).
 */
int ota_spike_run(const char *url);

#ifdef __cplusplus
}
#endif
