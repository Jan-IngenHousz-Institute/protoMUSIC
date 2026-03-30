#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "CLI.h"
#include "ambyte_mqtt_client.h"
#include "ambyte_status.h"
#include "device_config.h"
#include "bme280.h"
#include "device_commands.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "i2c_bus.h"
#include "lua_runner.h"
#include "nvs_flash.h"
#include "pcf2131tfy_rtc_api.h"
#include "sd_card.h"
#include "sqlite_persistence.h"
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

    /* ── Check provisioning state ─────────────────────────────────── */
    bool provisioned = false;
    err = wifi_manager_is_provisioned(&provisioned);
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "Provisioning check failed: %s", esp_err_to_name(err));
    }

    if (!provisioned) {
        ESP_LOGI(APP_TAG, "Device not provisioned — starting BLE provisioning");
        err = wifi_manager_start_provisioning("AMBYTE", "ambyte123");
        if (err == ESP_OK) {
            ESP_LOGI(APP_TAG, "WiFi provisioned and connected via BLE");
            return ESP_OK;
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(APP_TAG, "BLE provisioning timed out; continuing without WiFi");
        } else {
            ESP_LOGW(APP_TAG, "BLE provisioning failed: %s", esp_err_to_name(err));
        }
        return ESP_OK;
    }

    /* ── Already provisioned — connect with NVS-stored credentials ─ */
    err = wifi_manager_connect_stored();
    if (err == ESP_OK) {
        ESP_LOGI(APP_TAG, "Wi-Fi connected");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(APP_TAG, "Wi-Fi initial connect timed out; reconnect continues in background");
    } else {
        ESP_LOGW(APP_TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
    }

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
    vTaskDelay(pdMS_TO_TICKS(5000));   // wait 2000 ms
    ESP_LOGI(APP_TAG, "app_main entered");

    /* ── NVS ──────────────────────────────────────────────────────── */
    esp_err_t err = app_init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "NVS initialized");
    ESP_LOGI(APP_TAG, "Free heap after NVS: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── Runtime device config (NVS) ─────────────────────────────────── */
    err = device_config_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "device_config init failed: %s — using compile-time defaults",
                 esp_err_to_name(err));
    }

    /* ── MQTT Client — runtime config first, Kconfig fallback ──────── */
    char mqtt_uri[256], mqtt_client_id[64];
    if (device_config_get_mqtt_uri(mqtt_uri, sizeof(mqtt_uri)) != ESP_OK) {
        strncpy(mqtt_uri, CONFIG_AMBYTE_MQTT_URI, sizeof(mqtt_uri) - 1);
        mqtt_uri[sizeof(mqtt_uri) - 1] = '\0';
    }
    if (device_config_get_mqtt_client_id(mqtt_client_id, sizeof(mqtt_client_id)) != ESP_OK) {
        strncpy(mqtt_client_id, CONFIG_AMBYTE_MQTT_CLIENT_ID, sizeof(mqtt_client_id) - 1);
        mqtt_client_id[sizeof(mqtt_client_id) - 1] = '\0';
    }
    mqtt_client_config_t mqtt_cfg = {
        .broker_uri = mqtt_uri,
        .client_id  = mqtt_client_id,
    };
    err = mqtt_client_init(&mqtt_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "MQTT client init failed: %s — MQTT disabled", esp_err_to_name(err));
    }

    /* ── WiFi ─────────────────────────────────────────────────────── */
    err = app_start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "Free heap after WiFi: %lu", (unsigned long)esp_get_free_heap_size());

    /* ── I2C + Sensors ────────────────────────────────────────────── */
    err = app_init_i2c_and_sensors();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "I2C startup failed: %s", esp_err_to_name(err));
        return;
    }

    /* ── Status LED ───────────────────────────────────────────────── */
    err = ambyte_status_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "Status LED init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "Status LED initialized");
    }

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

    /* ── Persistence (SQLite + pending store) ─────────────────────── */
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

    /* ── Hardware inventory ────────────────────────────────────────── */
    ESP_LOGI(APP_TAG, "BOOT: BME280=%s RTC=%s SD=%s LFS=%s DB=%s",
             bme280_is_ready() ? "OK" : "ABSENT",
             pcf2131tfy_rtc_is_ready() ? "OK" : "ABSENT",
             sd_available ? "OK" : "ABSENT",
             lfs_available ? "OK" : "ABSENT",
             persistence_available ? "OK" : "ABSENT");

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
        .publish                 = mqtt_client_get_publish_fn(),
        .message_is_connected    = mqtt_client_get_is_connected_fn(),
        .set_publish_ack_handler = mqtt_client_get_set_ack_handler_fn(),
        .subscribe               = mqtt_client_get_subscribe_fn(),
    };
    device_commands_init(&cmd_cfg);
    device_commands_subscribe_inbound();

    /* ── Wi-Fi → MQTT lifecycle event handlers ────────────────────── */
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,          on_got_ip,         NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,   on_wifi_disconnect, NULL);

    /* Start MQTT now if WiFi was already connected during app_start_wifi */
    if (wifi_manager_is_connected()) {
        ESP_LOGI(APP_TAG, "WiFi already up — starting MQTT");
        mqtt_client_start();
    }

    /* ── Start application tasks ──────────────────────────────────── */
    app_start_lua_runner();
    app_start_cli();

    ESP_LOGI(APP_TAG, "Startup sequence complete, free heap: %lu",
             (unsigned long)esp_get_free_heap_size());
}
