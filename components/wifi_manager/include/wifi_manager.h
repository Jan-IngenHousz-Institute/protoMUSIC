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
 * @brief Clear Wi-Fi credentials and provisioning state, then reboot.
 *        Clears NVS namespace "wifi_prov" and restores Wi-Fi factory config.
 *        Never returns — calls esp_restart() internally.
 */
esp_err_t wifi_manager_clear_provisioning(void);

#ifdef __cplusplus
}
#endif
