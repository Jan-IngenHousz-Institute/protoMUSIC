#include "ambyte_mqtt_client.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define TAG                  "mqtt_client"

static esp_mqtt_client_handle_t s_client    = NULL;
static volatile bool            s_connected = false;
static bool                     s_started   = false;

/* Outbound ack callback */
static message_publish_ack_fn s_ack_handler = NULL;
static void                  *s_ack_ctx     = NULL;

/* ── port implementations ──────────────────────────────────────────── */

static esp_err_t mqtt_publish_impl(const char *topic, const char *payload,
                                   size_t len, int *out_msg_id)
{
    if (s_client == NULL || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, (int)len, 1, 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    if (out_msg_id != NULL) {
        *out_msg_id = msg_id;
    }
    return ESP_OK;
}

static bool mqtt_is_connected_impl(void)
{
    return s_connected;
}

static esp_err_t mqtt_set_ack_handler_impl(message_publish_ack_fn handler, void *ctx)
{
    s_ack_handler = handler;
    s_ack_ctx     = ctx;
    return ESP_OK;
}

/* ── MQTT event handler ────────────────────────────────────────────── */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT publish ack msg_id=%d", event->msg_id);
        if (s_ack_handler != NULL) {
            s_ack_handler(event->msg_id, ESP_OK, s_ack_ctx);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        if (s_ack_handler != NULL && event->msg_id > 0) {
            s_ack_handler(event->msg_id, ESP_FAIL, s_ack_ctx);
        }
        break;

    default:
        break;
    }
}

/* ── public API ────────────────────────────────────────────────────── */

esp_err_t mqtt_client_init(const mqtt_client_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool tls_ok = cfg->ca_cert_pem     != NULL && cfg->ca_cert_pem[0]     != '\0' &&
                  cfg->device_cert_pem != NULL && cfg->device_cert_pem[0] != '\0' &&
                  cfg->device_key_pem  != NULL && cfg->device_key_pem[0]  != '\0';

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri              = cfg->broker_uri,
            .verification.certificate = tls_ok ? cfg->ca_cert_pem : NULL,
        },
        .credentials = {
            .client_id = cfg->client_id,
            .authentication = {
                .certificate = tls_ok ? cfg->device_cert_pem : NULL,
                .key         = tls_ok ? cfg->device_key_pem  : NULL,
            },
        },
        .session = {
            .protocol_ver = MQTT_PROTOCOL_V_5,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client initialised (TLS=%s)", tls_ok ? "yes" : "no");
    return ESP_OK;
}

void mqtt_client_start(void)
{
    if (s_client != NULL && !s_started) {
        esp_mqtt_client_start(s_client);
        s_started = true;
    }
}

void mqtt_client_stop(void)
{
    /* Idempotent: silently skip if we never started. Prevents the esp-mqtt
     * "Client asked to stop, but was not started" warning on every Wi-Fi
     * disconnect attempt when the broker never came up. */
    if (s_client != NULL && s_started) {
        s_connected = false;
        s_started   = false;
        esp_mqtt_client_stop(s_client);
    }
}

bool mqtt_client_is_running(void)
{
    return s_started;
}

message_publish_fn mqtt_client_get_publish_fn(void)
{
    return mqtt_publish_impl;
}

message_is_connected_fn mqtt_client_get_is_connected_fn(void)
{
    return mqtt_is_connected_impl;
}

message_set_publish_ack_handler_fn mqtt_client_get_set_ack_handler_fn(void)
{
    return mqtt_set_ack_handler_impl;
}
