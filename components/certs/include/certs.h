#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the certs component — opens NVS namespace "certs" and loads
 * any previously stored PEM material into static buffers.
 * Must be called after nvs_flash_init(). */
esp_err_t certs_init(void);

/* Returns true only when all three PEM buffers are non-empty after certs_init(). */
bool certs_are_provisioned(void);

/* Getters — always return a non-NULL pointer; empty string if not provisioned. */
const char *certs_get_ca_cert(void);
const char *certs_get_device_cert(void);
const char *certs_get_device_key(void);

/* Setters — persist to NVS and update in-memory buffer immediately. */
esp_err_t certs_set_ca_cert(const char *pem);
esp_err_t certs_set_device_cert(const char *pem);
esp_err_t certs_set_device_key(const char *pem);

#ifdef __cplusplus
}
#endif
