#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "protocomm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extra BLE provisioning endpoint registered alongside the standard ones. */
typedef struct {
    const char              *name;
    protocomm_req_handler_t  handler;
    void                    *ctx;
} wifi_prov_extra_endpoint_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_connect_configured(void);
esp_err_t wifi_manager_connect_stored(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_is_provisioned(bool *out_provisioned);

/* Start BLE provisioning.  Pass extra_endpoints/num_extra_endpoints = NULL/0
 * if no custom endpoints are needed. */
esp_err_t wifi_manager_start_provisioning(
    const char *device_name, const char *pop,
    const wifi_prov_extra_endpoint_t *extra_endpoints,
    size_t num_extra_endpoints);

/**
 * @brief Signal that all provisioning data has been received over BLE.
 *
 * Unblocks wifi_manager_start_provisioning() immediately instead of waiting for
 * a Wi-Fi join. Joining while the BLE session is up races the shared 2.4 GHz
 * radio (Wi-Fi/BLE coexistence) and routinely fails at the auth handshake; the
 * caller is expected to reboot so the next boot joins with BLE off. Safe to
 * call from any task; a no-op if provisioning is not running.
 */
void wifi_manager_finish_provisioning(void);

/**
 * @brief Clear Wi-Fi credentials and provisioning state, then reboot.
 *        Clears NVS namespace "wifi_prov" and restores Wi-Fi factory config.
 *        Never returns — calls esp_restart() internally.
 */
esp_err_t wifi_manager_clear_provisioning(void);

#ifdef __cplusplus
}
#endif
