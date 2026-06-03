#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "CLI.h"
#include "ambyte_mqtt_client.h"
#include "ambyte_status.h"
#include "certs.h"
#include "device_config.h"
#include "bme280.h"
#include "device_commands.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "i2c_bus.h"
#include "lua_runner.h"
#include "nvs_flash.h"
#include "pcf2131tfy_rtc_api.h"
#include "sd_card.h"
#include "sqlite_persistence.h"
#include "uart_sensors.h"
#include "wifi_manager.h"

#define APP_TAG "APP_MAIN"

static const i2c_bus_config_t s_i2c_bus_cfg = {
    .port = I2C_BUS_DEFAULT_PORT,
    .sda_gpio = I2C_BUS_DEFAULT_SDA_GPIO,
    .scl_gpio = I2C_BUS_DEFAULT_SCL_GPIO,
    .clock_speed_hz = I2C_BUS_DEFAULT_SPEED_HZ,
};

static esp_err_t app_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }

    return err;
}

/* ── BLE provisioning endpoint: chunked cert upload ──────────────── */

#define CERT_PROV_MAX_PAYLOAD 8192  /* 3 PEM blobs + JSON overhead */

typedef enum { CERT_PROV_IDLE, CERT_PROV_RECEIVING } cert_prov_state_t;

static cert_prov_state_t s_cert_state = CERT_PROV_IDLE;
static char             *s_cert_buf   = NULL;
static size_t            s_cert_cap   = 0;
static size_t            s_cert_fill  = 0;

/* Provisioning completion tracking. Each flag is set by exactly one source:
 * the cert-prov endpoint, the dev-cfg endpoint, and the WIFI_PROV_CRED_RECV
 * event. Once all three provisioning payloads have arrived, provisioning is
 * finished early (prov_maybe_finish) so the device can reboot and join Wi-Fi
 * with BLE off — joining under BLE coexistence routinely fails the auth
 * handshake. */
static bool s_certs_written       = false;
static bool s_config_written      = false;
static bool s_wifi_creds_received = false;

static void prov_maybe_finish(void)
{
    if (s_certs_written && s_config_written && s_wifi_creds_received) {
        wifi_manager_finish_provisioning();
    }
}

static esp_err_t cert_prov_respond(uint8_t **outbuf, ssize_t *outlen, const char *json)
{
    size_t len = strlen(json);
    *outbuf = (uint8_t *)malloc(len + 1);
    if (*outbuf == NULL) {
        *outlen = 0;
        return ESP_ERR_NO_MEM;
    }
    memcpy(*outbuf, json, len + 1);
    *outlen = (ssize_t)len;
    return ESP_OK;
}

void cert_prov_handler_reset(void)
{
    free(s_cert_buf);
    s_cert_buf   = NULL;
    s_cert_cap   = 0;
    s_cert_fill  = 0;
    s_cert_state = CERT_PROV_IDLE;
}

static esp_err_t cert_prov_handler(uint32_t session_id,
                                    const uint8_t *inbuf, ssize_t inlen,
                                    uint8_t **outbuf, ssize_t *outlen,
                                    void *priv_data)
{
    (void)session_id;

    if (s_cert_state == CERT_PROV_IDLE) {
        /* First chunk: 4-byte big-endian total_len header */
        if (inlen < 4) {
            return cert_prov_respond(outbuf, outlen, "{\"error\":\"short header\"}");
        }
        uint32_t total_len = ((uint32_t)inbuf[0] << 24) |
                             ((uint32_t)inbuf[1] << 16) |
                             ((uint32_t)inbuf[2] <<  8) |
                             ((uint32_t)inbuf[3]);
        if (total_len == 0 || total_len > CERT_PROV_MAX_PAYLOAD) {
            return cert_prov_respond(outbuf, outlen, "{\"error\":\"invalid length\"}");
        }
        s_cert_buf = malloc(total_len + 1);
        if (s_cert_buf == NULL) {
            return cert_prov_respond(outbuf, outlen, "{\"error\":\"OOM\"}");
        }
        s_cert_cap   = total_len;
        s_cert_fill  = 0;
        s_cert_state = CERT_PROV_RECEIVING;
        inbuf += 4;
        inlen -= 4;
    }

    /* Append chunk (clamp to avoid overrun on broker misbehaviour) */
    size_t chunk = (size_t)(inlen > 0 ? inlen : 0);
    if (s_cert_fill + chunk > s_cert_cap) {
        chunk = s_cert_cap - s_cert_fill;
    }
    if (chunk > 0) {
        memcpy(s_cert_buf + s_cert_fill, inbuf, chunk);
        s_cert_fill += chunk;
    }

    if (s_cert_fill < s_cert_cap) {
        return cert_prov_respond(outbuf, outlen, "{\"complete\":false}");
    }

    /* All data received — parse and store */
    s_cert_buf[s_cert_fill] = '\0';
    cJSON *root = cJSON_Parse(s_cert_buf);
    cert_prov_handler_reset();

    if (root == NULL) {
        return cert_prov_respond(outbuf, outlen, "{\"ok\":false,\"error\":\"JSON parse failed\"}");
    }

    cJSON *ca   = cJSON_GetObjectItemCaseSensitive(root, "ca_cert");
    cJSON *cert = cJSON_GetObjectItemCaseSensitive(root, "dev_cert");
    cJSON *key  = cJSON_GetObjectItemCaseSensitive(root, "dev_key");

    esp_err_t err = ESP_OK;
    if (cJSON_IsString(ca)   && ca->valuestring   != NULL) {
        err = certs_set_ca_cert(ca->valuestring);
    }
    if (err == ESP_OK && cJSON_IsString(cert) && cert->valuestring != NULL) {
        err = certs_set_device_cert(cert->valuestring);
    }
    if (err == ESP_OK && cJSON_IsString(key)  && key->valuestring  != NULL) {
        err = certs_set_device_key(key->valuestring);
    }

    cJSON_Delete(root);

    if (err != ESP_OK) {
        return cert_prov_respond(outbuf, outlen, "{\"ok\":false,\"error\":\"NVS write failed\"}");
    }
    if (priv_data != NULL) {
        *(bool *)priv_data = true;
    }
    prov_maybe_finish();
    return cert_prov_respond(outbuf, outlen, "{\"ok\":true}");
}

/* ── BLE provisioning endpoint: device config ────────────────────── */

static esp_err_t dev_cfg_prov_handler(uint32_t session_id,
                                       const uint8_t *inbuf, ssize_t inlen,
                                       uint8_t **outbuf, ssize_t *outlen,
                                       void *priv_data)
{
    (void)session_id;

    cJSON *root = cJSON_ParseWithLength((const char *)inbuf, (size_t)inlen);
    if (root == NULL) {
        return cert_prov_respond(outbuf, outlen, "{\"ok\":false,\"error\":\"JSON parse failed\"}");
    }

    cJSON *uri    = cJSON_GetObjectItemCaseSensitive(root, "mqtt_uri");
    cJSON *cid    = cJSON_GetObjectItemCaseSensitive(root, "mqtt_client_id");
    cJSON *topic  = cJSON_GetObjectItemCaseSensitive(root, "mqtt_topic_root");
    cJSON *dev_id = cJSON_GetObjectItemCaseSensitive(root, "device_id");

    cJSON *prot_id   = cJSON_GetObjectItemCaseSensitive(root, "protocol_id");
    cJSON *dev_name  = cJSON_GetObjectItemCaseSensitive(root, "device_name");
    cJSON *dev_ver   = cJSON_GetObjectItemCaseSensitive(root, "device_version");
    cJSON *dev_firm  = cJSON_GetObjectItemCaseSensitive(root, "device_firmware");
    cJSON *firm_ver  = cJSON_GetObjectItemCaseSensitive(root, "firmware_version");

    if (cJSON_IsString(uri)      && uri->valuestring)      device_config_set_mqtt_uri(uri->valuestring);
    if (cJSON_IsString(cid)      && cid->valuestring)      device_config_set_mqtt_client_id(cid->valuestring);
    if (cJSON_IsString(topic)    && topic->valuestring)    device_config_set_mqtt_topic_root(topic->valuestring);
    if (cJSON_IsString(dev_id)   && dev_id->valuestring)   device_config_set_device_id(dev_id->valuestring);
    if (cJSON_IsString(prot_id)  && prot_id->valuestring)  device_config_set_protocol_id(prot_id->valuestring);
    if (cJSON_IsString(dev_name) && dev_name->valuestring) device_config_set_device_name(dev_name->valuestring);
    if (cJSON_IsString(dev_ver)  && dev_ver->valuestring)  device_config_set_device_version(dev_ver->valuestring);
    if (cJSON_IsString(dev_firm) && dev_firm->valuestring) device_config_set_device_firmware(dev_firm->valuestring);
    if (cJSON_IsString(firm_ver) && firm_ver->valuestring) device_config_set_firmware_version(firm_ver->valuestring);

    cJSON_Delete(root);
    if (priv_data != NULL) {
        *(bool *)priv_data = true;
    }
    prov_maybe_finish();
    return cert_prov_respond(outbuf, outlen, "{\"ok\":true}");
}

/* ── Wi-Fi init + start (provisioning/connect logic in app_main) ─── */

static esp_err_t app_start_wifi(void)
{
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "Wi-Fi manager initialized");

    err = wifi_manager_start();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "Wi-Fi station started");
    return ESP_OK;
}

static esp_err_t app_init_i2c_and_sensors(void)
{
    esp_err_t err = i2c_bus_init(&s_i2c_bus_cfg);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "I2C bus initialized");

    err = pcf2131tfy_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "RTC init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "RTC initialized");
    }

    if (pcf2131tfy_rtc_is_ready()) {
        time_t rtc_now = 0;
        if (pcf2131tfy_rtc_get_time(&rtc_now) == ESP_OK) {
            ESP_LOGI(APP_TAG, "RTC time: %lld", (long long)rtc_now);
        } else {
            ESP_LOGW(APP_TAG, "RTC time read failed");
        }
    }

    err = bme280_init(BME280_I2C_ADDR_SECONDARY);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "BME280 init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "BME280 initialized");
    }

    return ESP_OK;
}

static esp_err_t app_init_sdcard(void)
{
    esp_err_t err = sdcard_init_default();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "SD card init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "SD card mount failed, retrying in 500ms...");
        vTaskDelay(pdMS_TO_TICKS(500));
        err = sdcard_mount();
        if (err != ESP_OK) {
            ESP_LOGW(APP_TAG, "SD card mount retry failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(APP_TAG, "SD card mounted");
    return ESP_OK;
}

static esp_err_t app_init_littlefs(void)
{
    esp_vfs_littlefs_conf_t lfs_conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .grow_on_mount = true,
    };

    esp_err_t err = esp_vfs_littlefs_register(&lfs_conf);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(APP_TAG, "LittleFS mounted");
    return ESP_OK;
}

static void app_start_lua_runner(void)
{
    const esp_err_t err = lua_runner_start();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "Lua runner failed to start: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(APP_TAG, "Lua runner started");
}

static void app_start_cli(void)
{
    const esp_err_t err = cli_start();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "CLI start failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(APP_TAG, "CLI started");
}

/* ── BLE provisioning LED feedback handler ───────────────────────── */

static void prov_led_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch ((wifi_prov_cb_event_t)id) {
    case WIFI_PROV_CRED_RECV:
        /* Wi-Fi credentials delivered — one of the three provisioning payloads.
         * Finish provisioning early once the config and certs have also landed. */
        s_wifi_creds_received = true;
        prov_maybe_finish();
        break;
    case WIFI_PROV_CRED_FAIL:
        ambyte_status_set_rgb(20, 0, 0);  /* red = wrong password */
        break;
    case WIFI_PROV_CRED_SUCCESS:
        ambyte_status_set_rgb(0, 20, 0);  /* green = credentials accepted */
        break;
    default:
        break;
    }
}

/* ── Wi-Fi → MQTT lifecycle handlers ─────────────────────────────── */

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    ESP_LOGI(APP_TAG, "IP acquired — starting MQTT");
    mqtt_client_start();
}

static void on_wifi_disconnect(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    ESP_LOGW(APP_TAG, "Wi-Fi disconnected — stopping MQTT");
    mqtt_client_stop();
    device_commands_on_mqtt_disconnect();
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(APP_TAG, "app_main entered");

    /* ── NVS ──────────────────────────────────────────────────────── */
    esp_err_t err = app_init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "NVS initialized");
    ESP_LOGI(APP_TAG, "Free heap after NVS: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── Status LED (early init — needed before BLE provisioning) ── */
    err = ambyte_status_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "Status LED init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "Status LED initialized");
    }

    /* ── Certs (NVS-backed) ──────────────────────────────────────── */
    err = certs_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "certs_init failed: %s — TLS disabled", esp_err_to_name(err));
    }

    /* ── Runtime device config (NVS) ─────────────────────────────── */
    err = device_config_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "device_config init failed: %s — using compile-time defaults",
                 esp_err_to_name(err));
    }

    /* ── Resolve config: NVS first, Kconfig fallback ─────────────── */
    static char mqtt_uri[256], mqtt_client_id[64], topic_root[256], device_id[64];
    if (device_config_get_mqtt_uri(mqtt_uri, sizeof(mqtt_uri)) != ESP_OK) {
        strncpy(mqtt_uri, CONFIG_AMBYTE_MQTT_URI, sizeof(mqtt_uri) - 1);
        mqtt_uri[sizeof(mqtt_uri) - 1] = '\0';
    }
    if (device_config_get_mqtt_client_id(mqtt_client_id, sizeof(mqtt_client_id)) != ESP_OK) {
        strncpy(mqtt_client_id, CONFIG_AMBYTE_MQTT_CLIENT_ID, sizeof(mqtt_client_id) - 1);
        mqtt_client_id[sizeof(mqtt_client_id) - 1] = '\0';
    }
    if (device_config_get_mqtt_topic_root(topic_root, sizeof(topic_root)) != ESP_OK) {
        strncpy(topic_root, CONFIG_AMBYTE_MQTT_TOPIC_ROOT, sizeof(topic_root) - 1);
        topic_root[sizeof(topic_root) - 1] = '\0';
    }
    if (device_config_get_device_id(device_id, sizeof(device_id)) != ESP_OK) {
        strncpy(device_id, CONFIG_AMBYTE_DEVICE_ID, sizeof(device_id) - 1);
        device_id[sizeof(device_id) - 1] = '\0';
    }
    static char protocol_id[32], device_name[64], device_version[16],
                device_firmware[16], firmware_version[16];
    if (device_config_get_protocol_id(protocol_id, sizeof(protocol_id)) != ESP_OK) {
        protocol_id[0] = '\0';
    }
    if (device_config_get_device_name(device_name, sizeof(device_name)) != ESP_OK) {
        device_name[0] = '\0';
    }
    if (device_config_get_device_version(device_version, sizeof(device_version)) != ESP_OK) {
        device_version[0] = '\0';
    }
    if (device_config_get_device_firmware(device_firmware, sizeof(device_firmware)) != ESP_OK) {
        device_firmware[0] = '\0';
    }
    if (device_config_get_firmware_version(firmware_version, sizeof(firmware_version)) != ESP_OK) {
        firmware_version[0] = '\0';
    }

    /* ── MQTT Client ─────────────────────────────────────────────── */
    bool certs_ok = certs_are_provisioned();
    mqtt_client_config_t mqtt_cfg = {
        .broker_uri      = mqtt_uri,
        .client_id       = mqtt_client_id,
        .ca_cert_pem     = certs_ok ? certs_get_ca_cert()     : NULL,
        .device_cert_pem = certs_ok ? certs_get_device_cert() : NULL,
        .device_key_pem  = certs_ok ? certs_get_device_key()  : NULL,
    };
    err = mqtt_client_init(&mqtt_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "MQTT client init failed: %s — MQTT disabled", esp_err_to_name(err));
    }

    /* ── Wi-Fi init + start ───────────────────────────────────────── */
    err = app_start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "Free heap after WiFi: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── Provisioning or connect ─────────────────────────────────── */
    bool provisioned = false;
    err = wifi_manager_is_provisioned(&provisioned);
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "Provisioning check failed: %s", esp_err_to_name(err));
    }

    if (!provisioned) {
        ESP_LOGI(APP_TAG, "Device not provisioned — starting BLE provisioning");
        /* s_certs_written / s_config_written are file-scope (see prov_maybe_finish). */
        wifi_prov_extra_endpoint_t endpoints[] = {
            { "cert-prov", cert_prov_handler,    &s_certs_written  },
            { "dev-cfg",   dev_cfg_prov_handler, &s_config_written },
        };
        /* Register LED handler for provisioning events and signal BLE active */
        esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                   prov_led_event_handler, NULL);
        ambyte_status_set_rgb(0, 0, 20);  /* dim blue = BLE advertising */
        esp_err_t prov_err = wifi_manager_start_provisioning("AMBYTE", "ambyte123",
                                                              endpoints, 2);
        cert_prov_handler_reset();  /* free any partial cert buffer */
        bool any_write = s_certs_written || s_config_written;
        bool prov_ok   = (prov_err == ESP_OK);
        if (prov_ok || any_write) {
            ESP_LOGI(APP_TAG, "Provisioning complete — rebooting to apply");
            esp_restart();
        }
        ambyte_status_set_rgb(0, 0, 0);   /* LED off — continuing without reboot */
        ESP_LOGW(APP_TAG, "BLE provisioning timed out with no writes — continuing");
    } else {
        err = wifi_manager_connect_stored();
        if (err == ESP_OK) {
            ESP_LOGI(APP_TAG, "Wi-Fi connected");
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(APP_TAG, "Wi-Fi initial connect timed out; reconnect continues in background");
        } else {
            ESP_LOGW(APP_TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
        }
    }

    /* ── Wi-Fi → MQTT lifecycle event handlers ────────────────────── */
    /* Registered after the provisioning/connect block so they cannot fire
     * during provisioning and trigger an MQTT start mid-session. */
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,         on_got_ip,          NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,  on_wifi_disconnect, NULL);

    /* Start MQTT now if WiFi was already connected during the connect block */
    if (wifi_manager_is_connected()) {
        ESP_LOGI(APP_TAG, "WiFi already up — starting MQTT");
        mqtt_client_start();
    }

    /* ── I2C + Sensors ────────────────────────────────────────────── */
    err = app_init_i2c_and_sensors();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "I2C startup failed: %s", esp_err_to_name(err));
        return;
    }

    /* ── UART Sensors (4 channels, Option D) ────────────────────── */
    bool uart_available = false;
    err = uart_sensors_init();
    if (err == ESP_OK) {
        uart_available = true;
        ESP_LOGI(APP_TAG, "UART sensors initialized");
        /* Auto-ping each channel at boot (up to 2 s per channel) */
        for (uint8_t ch = 0; ch < UART_SENSOR_NUM_CHANNELS; ch++) {
            bool conn = false;
            uart_sensors_get_ping_fn()(ch, &conn);
            ESP_LOGI(APP_TAG, "  AMBIT%u: %s", ch + 1,
                     conn ? "connected" : "disconnected");
        }
    } else {
        ESP_LOGW(APP_TAG, "UART sensors init failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(APP_TAG, "Free heap after UART: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── SD Card ──────────────────────────────────────────────────── */
    bool sd_available = false;
    err = app_init_sdcard();
    if (err == ESP_OK) {
        sd_available = true;
    } else {
        ESP_LOGW(APP_TAG, "SD card unavailable, buffering to LittleFS only");
    }

    /* ── LittleFS ─────────────────────────────────────────────────── */
    bool lfs_available = false;
    err = app_init_littlefs();
    if (err == ESP_OK) {
        lfs_available = true;
    } else {
        ESP_LOGE(APP_TAG, "LittleFS unavailable, persistence disabled");
    }

    /* ── Persistence (SQLite) ─────────────────────────────────────── */
    bool persistence_available = false;
    if (lfs_available) {
        err = sqlite_persistence_init();
        if (err == ESP_OK) {
            persistence_available = true;
            ESP_LOGI(APP_TAG, "Persistence layer ready");
        } else {
            ESP_LOGW(APP_TAG, "Persistence init failed: %s", esp_err_to_name(err));
        }
    }
    ESP_LOGI(APP_TAG, "Free heap after persistence: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── Hardware inventory ───────────────────────────────────────── */
    ESP_LOGI(APP_TAG, "BOOT: BME280=%s RTC=%s SD=%s LFS=%s DB=%s UART=%s",
             bme280_is_ready() ? "OK" : "ABSENT",
             pcf2131tfy_rtc_is_ready() ? "OK" : "ABSENT",
             sd_available ? "OK" : "ABSENT",
             lfs_available ? "OK" : "ABSENT",
             persistence_available ? "OK" : "ABSENT",
             uart_available ? "OK" : "ABSENT");

    /* ── Compose device_commands (DDD composition root) ───────────── */
    device_commands_config_t cmd_cfg = {
        .read_env               = bme280_get_sensor_read_fn(),
        .read_clock             = pcf2131tfy_rtc_get_clock_read_fn(),
        .set_status             = ambyte_status_get_set_fn(),
        .store                  = persistence_available ? sqlite_persistence_get_store_fn()              : NULL,
        .query                  = persistence_available ? sqlite_persistence_get_query_fn()              : NULL,
        .count                  = persistence_available ? sqlite_persistence_get_count_fn()              : NULL,
        .next_id                = persistence_available ? sqlite_persistence_get_next_id_fn()            : NULL,
        .query_unsynced         = persistence_available ? sqlite_persistence_get_query_unsynced_fn()     : NULL,
        .mark_synced            = persistence_available ? sqlite_persistence_get_mark_synced_fn()        : NULL,
        .mark_inflight          = persistence_available ? sqlite_persistence_get_mark_inflight_fn()      : NULL,
        .mark_pending           = persistence_available ? sqlite_persistence_get_mark_pending_fn()       : NULL,
        .query_by_id            = persistence_available ? sqlite_persistence_get_query_by_id_fn()        : NULL,
        .claim_next_pending     = persistence_available ? sqlite_persistence_get_claim_next_pending_fn() : NULL,
        .publish                = mqtt_client_get_publish_fn(),
        .message_is_connected   = mqtt_client_get_is_connected_fn(),
        .set_publish_ack_handler = mqtt_client_get_set_ack_handler_fn(),
        .subscribe              = mqtt_client_get_subscribe_fn(),
        .topic_root             = topic_root,
        .device_id              = device_id,
        .certs_status           = certs_are_provisioned,
        .protocol_id            = protocol_id,
        .device_name            = device_name,
        .device_version         = device_version,
        .device_firmware        = device_firmware,
        .firmware_version       = firmware_version,
        .uart_query             = uart_available ? uart_sensors_get_query_fn()  : NULL,
        .uart_ping              = uart_available ? uart_sensors_get_ping_fn()   : NULL,
        .uart_status            = uart_available ? uart_sensors_get_status_fn() : NULL,
        .uart_text_query        = uart_available ? uart_sensors_get_text_query_fn() : NULL,
    };
    device_commands_init(&cmd_cfg);
    // device_commands_subscribe_inbound();

    /* ── Start application tasks ──────────────────────────────────── */
    app_start_lua_runner();
    app_start_cli();

    ESP_LOGI(APP_TAG, "Startup sequence complete, free heap: %lu",
             (unsigned long)esp_get_free_heap_size());
}
