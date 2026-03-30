#include "device_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG          "device_cfg"
#define NVS_NS       "device_cfg"

/* NVS key names — max 15 chars (NVS_KEY_NAME_MAX_SIZE = 16 incl. null) */
#define KEY_MQTT_URI        "mqtt_uri"
#define KEY_MQTT_CLIENT_ID  "mqtt_client_id"
#define KEY_MQTT_TOPIC_ROOT "mqtt_topic_root"

static nvs_handle_t s_handle    = 0;
static bool         s_initialized = false;

esp_err_t device_config_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "device_config initialised");
    return ESP_OK;
}

static esp_err_t cfg_get(const char *key, char *buf, size_t len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    size_t out_len = len;
    return nvs_get_str(s_handle, key, buf, &out_len);
}

static esp_err_t cfg_set(const char *key, const char *val)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_str(s_handle, key, val);
    if (err != ESP_OK) return err;
    return nvs_commit(s_handle);
}

esp_err_t device_config_get_mqtt_uri(char *buf, size_t len)
{
    return cfg_get(KEY_MQTT_URI, buf, len);
}

esp_err_t device_config_get_mqtt_client_id(char *buf, size_t len)
{
    return cfg_get(KEY_MQTT_CLIENT_ID, buf, len);
}

esp_err_t device_config_get_mqtt_topic_root(char *buf, size_t len)
{
    return cfg_get(KEY_MQTT_TOPIC_ROOT, buf, len);
}

esp_err_t device_config_set_mqtt_uri(const char *val)
{
    return cfg_set(KEY_MQTT_URI, val);
}

esp_err_t device_config_set_mqtt_client_id(const char *val)
{
    return cfg_set(KEY_MQTT_CLIENT_ID, val);
}

esp_err_t device_config_set_mqtt_topic_root(const char *val)
{
    return cfg_set(KEY_MQTT_TOPIC_ROOT, val);
}
