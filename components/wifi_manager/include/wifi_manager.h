#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_connect_configured(void);
esp_err_t wifi_manager_connect_stored(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_is_provisioned(bool *out_provisioned);
esp_err_t wifi_manager_start_provisioning(const char *device_name, const char *pop);

#ifdef __cplusplus
}
#endif
