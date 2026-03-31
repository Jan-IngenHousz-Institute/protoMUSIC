#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime device configuration stored in NVS namespace "device_cfg".
 * Returns ESP_ERR_NOT_FOUND when a key is absent — caller falls back to
 * compile-time CONFIG_AMBYTE_* defaults.
 * NVS must be initialised (nvs_flash_init) before calling device_config_init.
 */

esp_err_t device_config_init(void);

/* Getters — return ESP_ERR_NOT_FOUND if not set in NVS */
esp_err_t device_config_get_mqtt_uri(char *buf, size_t len);
esp_err_t device_config_get_mqtt_client_id(char *buf, size_t len);
esp_err_t device_config_get_mqtt_topic_root(char *buf, size_t len);
esp_err_t device_config_get_device_id(char *buf, size_t len);
esp_err_t device_config_get_protocol_id(char *buf, size_t len);
esp_err_t device_config_get_device_name(char *buf, size_t len);
esp_err_t device_config_get_device_version(char *buf, size_t len);
esp_err_t device_config_get_device_firmware(char *buf, size_t len);
esp_err_t device_config_get_firmware_version(char *buf, size_t len);

/* Setters — persist immediately via nvs_commit */
esp_err_t device_config_set_mqtt_uri(const char *val);
esp_err_t device_config_set_mqtt_client_id(const char *val);
esp_err_t device_config_set_mqtt_topic_root(const char *val);
esp_err_t device_config_set_device_id(const char *val);
esp_err_t device_config_set_protocol_id(const char *val);
esp_err_t device_config_set_device_name(const char *val);
esp_err_t device_config_set_device_version(const char *val);
esp_err_t device_config_set_device_firmware(const char *val);
esp_err_t device_config_set_firmware_version(const char *val);

#ifdef __cplusplus
}
#endif
