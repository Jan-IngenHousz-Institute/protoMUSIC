#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "wifi_manager.h"

#define WIFI_MANAGER_CONNECTED_BIT BIT0
#define WIFI_MANAGER_FAILED_BIT BIT1
#define WIFI_MANAGER_PROV_DONE_BIT BIT2
#define WIFI_MANAGER_INITIAL_CONNECT_TIMEOUT_MS 10000
#define WIFI_MANAGER_PROV_TIMEOUT_MS 120000
#define WIFI_MANAGER_STA_IFKEY "WIFI_STA_DEF"
#define WIFI_MANAGER_UNPROVISIONED_PLACEHOLDER "__UNPROVISIONED__"
#define WIFI_MANAGER_PROV_NVS_NAMESPACE "wifi_prov"
#define WIFI_MANAGER_PROV_NVS_KEY "provisioned"

typedef struct {
    EventGroupHandle_t event_group;
    esp_netif_t *sta_netif;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    bool handlers_registered;
    bool initialized;
    bool started;
    bool connect_requested;
    bool reconfigure_in_progress;
    int reconnect_count;
    char current_ssid[33];
} wifi_manager_service_t;

static const char *TAG = "wifi_manager";
static wifi_manager_service_t s_wifi = {
    .event_group = NULL,
    .sta_netif = NULL,
    .wifi_handler = NULL,
    .ip_handler = NULL,
    .handlers_registered = false,
    .initialized = false,
    .started = false,
    .connect_requested = false,
    .reconfigure_in_progress = false,
    .reconnect_count = 0,
    .current_ssid = {0},
};

static bool wifi_manager_disconnect_reason_is_fatal(wifi_err_reason_t reason)
{
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_LEAVE:
        case WIFI_REASON_ASSOC_NOT_AUTHED:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_CONNECTION_FAIL:
            return true;
        default:
            return false;
    }
}

static bool wifi_manager_config_value_is_placeholder(const char *value)
{
    return (value == NULL) ||
           (strcmp(value, WIFI_MANAGER_UNPROVISIONED_PLACEHOLDER) == 0);
}

static esp_err_t wifi_manager_ensure_event_group(void)
{
    if (s_wifi.event_group != NULL) {
        return ESP_OK;
    }

    s_wifi.event_group = xEventGroupCreate();
    if (s_wifi.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t wifi_manager_ensure_sta_netif(void)
{
    if (s_wifi.sta_netif != NULL) {
        return ESP_OK;
    }

    s_wifi.sta_netif = esp_netif_get_handle_from_ifkey(WIFI_MANAGER_STA_IFKEY);
    if (s_wifi.sta_netif == NULL) {
        s_wifi.sta_netif = esp_netif_create_default_wifi_sta();
    }

    return (s_wifi.sta_netif != NULL) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wifi_manager_copy_config_string(
    char *dest,
    size_t dest_size,
    const char *src)
{
    const size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dest, 0, dest_size);
    memcpy(dest, src, src_len);
    return ESP_OK;
}

static esp_err_t wifi_manager_apply_config(const char *ssid, const char *password)
{
    if ((ssid == NULL) || (password == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_wifi.initialized || !s_wifi.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};

    esp_err_t err = wifi_manager_copy_config_string(
        (char *)wifi_config.sta.ssid,
        sizeof(wifi_config.sta.ssid),
        ssid);
    if (err != ESP_OK) {
        return err;
    }

    err = wifi_manager_copy_config_string(
        (char *)wifi_config.sta.password,
        sizeof(wifi_config.sta.password),
        password);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config.sta.threshold.authmode =
        (password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    return wifi_manager_copy_config_string(
        s_wifi.current_ssid,
        sizeof(s_wifi.current_ssid),
        ssid);
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        ESP_LOGI(TAG, "Wi-Fi station started");
        return;
    }

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;
        const wifi_err_reason_t reason =
            disconnected ? disconnected->reason : WIFI_REASON_UNSPECIFIED;

        xEventGroupClearBits(s_wifi.event_group, WIFI_MANAGER_CONNECTED_BIT);

        if (s_wifi.reconfigure_in_progress) {
            s_wifi.reconfigure_in_progress = false;
            ESP_LOGI(TAG, "Wi-Fi disconnected for reconfigure");
            return;
        }

        if (!s_wifi.connect_requested) {
            ESP_LOGW(
                TAG,
                "Wi-Fi disconnected (reason=%d)",
                reason);
            xEventGroupSetBits(s_wifi.event_group, WIFI_MANAGER_FAILED_BIT);
            return;
        }

        if (wifi_manager_disconnect_reason_is_fatal(reason)) {
            s_wifi.connect_requested = false;
            xEventGroupSetBits(s_wifi.event_group, WIFI_MANAGER_FAILED_BIT);
            ESP_LOGE(TAG, "Wi-Fi fatal disconnect (reason=%d) for \"%s\"", reason, s_wifi.current_ssid);
            return;
        }

        ++s_wifi.reconnect_count;
        ESP_LOGW(
            TAG,
            "Wi-Fi disconnected (reason=%d), reconnect attempt %d to \"%s\"",
            reason,
            s_wifi.reconnect_count,
            s_wifi.current_ssid);

        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            xEventGroupSetBits(s_wifi.event_group, WIFI_MANAGER_FAILED_BIT);
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        }
        return;
    }

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_CONNECTED)) {
        xEventGroupClearBits(s_wifi.event_group, WIFI_MANAGER_FAILED_BIT);
        ESP_LOGI(TAG, "Associated with AP \"%s\"", s_wifi.current_ssid);
        return;
    }

    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        s_wifi.reconnect_count = 0;
        xEventGroupSetBits(s_wifi.event_group, WIFI_MANAGER_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi.event_group, WIFI_MANAGER_FAILED_BIT);
        ESP_LOGI(TAG, "Got IP from AP");
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_wifi.initialized) {
        return ESP_OK;
    }

    esp_err_t err = wifi_manager_ensure_event_group();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }

    err = esp_event_loop_create_default();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }

    err = wifi_manager_ensure_sta_netif();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_cfg);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }

    if (!s_wifi.handlers_registered) {
        err = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            &s_wifi.wifi_handler);
        if (err != ESP_OK) {
            return err;
        }

        err = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            &s_wifi.ip_handler);
        if (err != ESP_OK) {
            esp_event_handler_instance_unregister(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                s_wifi.wifi_handler);
            s_wifi.wifi_handler = NULL;
            return err;
        }

        s_wifi.handlers_registered = true;
    }

    s_wifi.initialized = true;

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_wifi.started) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    s_wifi.started = true;
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if ((ssid == NULL) || (password == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_manager_apply_config(ssid, password);
    if (err != ESP_OK) {
        return err;
    }

    s_wifi.reconnect_count = 0;
    xEventGroupClearBits(s_wifi.event_group, WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT);

    s_wifi.connect_requested = false;
    s_wifi.reconfigure_in_progress = true;
    err = esp_wifi_disconnect();
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_CONNECT)) {
        s_wifi.reconfigure_in_progress = false;
        return err;
    }
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        s_wifi.reconfigure_in_progress = false;
    }

    s_wifi.connect_requested = true;

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        s_wifi.connect_requested = false;
        return err;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi.event_group,
        WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_MANAGER_INITIAL_CONNECT_TIMEOUT_MS));

    if ((bits & WIFI_MANAGER_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    if ((bits & WIFI_MANAGER_FAILED_BIT) != 0) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_connect_configured(void)
{
    if ((CONFIG_AMBYTE_WIFI_SSID[0] == '\0') ||
        wifi_manager_config_value_is_placeholder(CONFIG_AMBYTE_WIFI_SSID)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (wifi_manager_config_value_is_placeholder(CONFIG_AMBYTE_WIFI_PASSWORD)) {
        return ESP_ERR_NOT_FOUND;
    }

    return wifi_manager_connect(CONFIG_AMBYTE_WIFI_SSID, CONFIG_AMBYTE_WIFI_PASSWORD);
}

esp_err_t wifi_manager_connect_stored(void)
{
    if (!s_wifi.initialized || !s_wifi.started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = wifi_manager_ensure_event_group();
    if (err != ESP_OK) {
        return err;
    }

    s_wifi.reconnect_count = 0;
    xEventGroupClearBits(s_wifi.event_group,
                         WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT);
    s_wifi.connect_requested = true;

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        s_wifi.connect_requested = false;
        return err;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi.event_group,
        WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_MANAGER_INITIAL_CONNECT_TIMEOUT_MS));

    if ((bits & WIFI_MANAGER_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    if ((bits & WIFI_MANAGER_FAILED_BIT) != 0) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

bool wifi_manager_is_connected(void)
{
    if (s_wifi.event_group == NULL) {
        return false;
    }

    return (xEventGroupGetBits(s_wifi.event_group) & WIFI_MANAGER_CONNECTED_BIT) != 0;
}

/* ── Provisioning ────────────────────────────────────────────────────── */

static void wifi_prov_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started — waiting for BLE client");
                break;

            case WIFI_PROV_CRED_RECV: {
                const wifi_sta_config_t *cfg = (const wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received credentials for SSID: %s", (const char *)cfg->ssid);
                break;
            }

            case WIFI_PROV_CRED_FAIL: {
                const wifi_prov_sta_fail_reason_t *reason =
                    (const wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning credential failure: %d",
                         reason ? (int)*reason : -1);
                break;
            }

            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning credential success");
                break;

            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                if (s_wifi.event_group != NULL) {
                    xEventGroupSetBits(s_wifi.event_group, WIFI_MANAGER_PROV_DONE_BIT);
                }
                break;

            default:
                break;
        }
    }
}

esp_err_t wifi_manager_is_provisioned(bool *out_provisioned)
{
    if (out_provisioned == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_provisioned = false;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_MANAGER_PROV_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t val = 0;
    err = nvs_get_u8(nvs, WIFI_MANAGER_PROV_NVS_KEY, &val);
    nvs_close(nvs);

    if (err == ESP_OK && val == 1) {
        *out_provisioned = true;
    }

    return ESP_OK;
}

static esp_err_t wifi_manager_mark_provisioned(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_MANAGER_PROV_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, WIFI_MANAGER_PROV_NVS_KEY, 1);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t wifi_manager_start_provisioning(
    const char *device_name, const char *pop,
    const wifi_prov_extra_endpoint_t *extra_endpoints,
    size_t num_extra_endpoints)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_manager_ensure_event_group();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
        &wifi_prov_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE,
    };

    err = wifi_prov_mgr_init(prov_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register custom endpoint names before start (protocomm requires this order) */
    for (size_t i = 0; i < num_extra_endpoints; i++) {
        if (extra_endpoints[i].name != NULL) {
            wifi_prov_mgr_endpoint_create(extra_endpoints[i].name);
        }
    }

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *service_key = NULL;

    xEventGroupClearBits(s_wifi.event_group,
                         WIFI_MANAGER_CONNECTED_BIT |
                         WIFI_MANAGER_FAILED_BIT |
                         WIFI_MANAGER_PROV_DONE_BIT);

    err = wifi_prov_mgr_start_provisioning(security, pop, device_name, service_key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_start_provisioning failed: %s", esp_err_to_name(err));
        wifi_prov_mgr_deinit();
        return err;
    }

    /* Register handlers after start (protocomm session is now live) */
    for (size_t i = 0; i < num_extra_endpoints; i++) {
        if (extra_endpoints[i].name != NULL && extra_endpoints[i].handler != NULL) {
            wifi_prov_mgr_endpoint_register(extra_endpoints[i].name,
                                            extra_endpoints[i].handler,
                                            extra_endpoints[i].ctx);
        }
    }

    ESP_LOGI(TAG, "BLE provisioning started as \"%s\"", device_name);

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi.event_group,
        WIFI_MANAGER_PROV_DONE_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_MANAGER_PROV_TIMEOUT_MS));

    if ((bits & WIFI_MANAGER_PROV_DONE_BIT) == 0) {
        ESP_LOGW(TAG, "Provisioning timed out after %d ms", WIFI_MANAGER_PROV_TIMEOUT_MS);
        wifi_prov_mgr_stop_provisioning();
        wifi_prov_mgr_deinit();
        return ESP_ERR_TIMEOUT;
    }

    wifi_prov_mgr_deinit();

    wifi_manager_mark_provisioned();

    ESP_LOGI(TAG, "Provisioning complete, waiting for WiFi connection...");

    const EventBits_t conn_bits = xEventGroupWaitBits(
        s_wifi.event_group,
        WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_MANAGER_INITIAL_CONNECT_TIMEOUT_MS));

    if ((conn_bits & WIFI_MANAGER_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "WiFi connected after provisioning");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WiFi connection after provisioning did not complete in time");
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_clear_provisioning(void)
{
    /* Clear our custom provisioned flag */
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_MANAGER_PROV_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_erase_key(nvs, WIFI_MANAGER_PROV_NVS_KEY);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    /* Clear stored Wi-Fi credentials (factory reset WiFi config in NVS) */
    esp_wifi_restore();
    ESP_LOGI(TAG, "Wi-Fi provisioning cleared — rebooting");
    esp_restart();
    return ESP_OK; /* never reached */
}
