#include "ambyte_mqtt_client.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "certs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define TAG                  "mqtt_client"
#define MQTT_MAX_SUBSCRIPTIONS 8
#define MQTT_TOPIC_BUF_SIZE  128

typedef struct {
    char               topic[MQTT_TOPIC_BUF_SIZE];
    message_handler_fn handler;
} mqtt_sub_t;

static esp_mqtt_client_handle_t s_client    = NULL;
static volatile bool            s_connected = false;

/* Outbound ack callback */
static message_publish_ack_fn s_ack_handler = NULL;
static void                  *s_ack_ctx     = NULL;

/* Subscription table */
static mqtt_sub_t s_subs[MQTT_MAX_SUBSCRIPTIONS];
static size_t     s_sub_count = 0;

/* Fragmented payload reassembly — one in-flight at a time (MQTT events are serialised) */
static char  *s_reassembly_buf   = NULL;
static size_t s_reassembly_cap   = 0;
static size_t s_reassembly_fill  = 0;
static char   s_reassembly_topic[MQTT_TOPIC_BUF_SIZE];

/* ── helpers ───────────────────────────────────────────────────────── */

static void dispatch_to_handlers(const char *topic, const char *payload, size_t len)
{
    for (size_t i = 0; i < s_sub_count; i++) {
        if (strcmp(s_subs[i].topic, topic) == 0 && s_subs[i].handler != NULL) {
            s_subs[i].handler(topic, payload, len);
        }
    }
}

static void reassembly_reset(void)
{
    free(s_reassembly_buf);
    s_reassembly_buf  = NULL;
    s_reassembly_cap  = 0;
    s_reassembly_fill = 0;
    s_reassembly_topic[0] = '\0';
}

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

static esp_err_t mqtt_subscribe_impl(const char *topic, message_handler_fn handler)
{
    if (s_sub_count >= MQTT_MAX_SUBSCRIPTIONS) {
        ESP_LOGE(TAG, "subscription table full");
        return ESP_ERR_NO_MEM;
    }
    mqtt_sub_t *sub = &s_subs[s_sub_count++];
    strncpy(sub->topic, topic, sizeof(sub->topic) - 1);
    sub->topic[sizeof(sub->topic) - 1] = '\0';
    sub->handler = handler;

    /* Subscribe immediately if already connected */
    if (s_connected && s_client != NULL) {
        esp_mqtt_client_subscribe(s_client, topic, 1);
    }
    ESP_LOGI(TAG, "registered subscription: %s", topic);
    return ESP_OK;
}

/* ── MQTT event handler ────────────────────────────────────────────── */

static void handle_data_event(esp_mqtt_event_handle_t event)
{
    bool single_chunk = ((size_t)event->total_data_len == (size_t)event->data_len);

    if (single_chunk) {
        /* Fast path: no heap allocation needed */
        char topic[MQTT_TOPIC_BUF_SIZE];
        size_t tlen = (size_t)event->topic_len < sizeof(topic) - 1
                      ? (size_t)event->topic_len : sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = '\0';
        dispatch_to_handlers(topic, event->data, (size_t)event->data_len);
        return;
    }

    /* Fragmented path */
    if ((size_t)event->current_data_offset == 0) {
        /* First chunk — allocate reassembly buffer */
        reassembly_reset();
        size_t total = (size_t)event->total_data_len;
        s_reassembly_buf = malloc(total + 1);
        if (s_reassembly_buf == NULL) {
            ESP_LOGE(TAG, "OOM: cannot reassemble %u-byte payload", (unsigned)total);
            return;
        }
        s_reassembly_cap = total;
        /* Save topic (only present on first chunk) */
        size_t tlen = (size_t)event->topic_len < sizeof(s_reassembly_topic) - 1
                      ? (size_t)event->topic_len : sizeof(s_reassembly_topic) - 1;
        memcpy(s_reassembly_topic, event->topic, tlen);
        s_reassembly_topic[tlen] = '\0';
    }

    if (s_reassembly_buf == NULL) {
        return; /* OOM on first chunk — skip remaining chunks */
    }

    size_t offset  = (size_t)event->current_data_offset;
    size_t chunk   = (size_t)event->data_len;
    size_t end     = offset + chunk;
    if (end > s_reassembly_cap) {
        end = s_reassembly_cap; /* guard against broker misbehaviour */
        chunk = s_reassembly_cap - offset;
    }
    memcpy(s_reassembly_buf + offset, event->data, chunk);
    s_reassembly_fill = end;

    bool last_chunk = (end >= s_reassembly_cap);
    if (last_chunk) {
        s_reassembly_buf[s_reassembly_fill] = '\0';
        dispatch_to_handlers(s_reassembly_topic, s_reassembly_buf, s_reassembly_fill);
        reassembly_reset();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected — re-subscribing %u topic(s)", (unsigned)s_sub_count);
        for (size_t i = 0; i < s_sub_count; i++) {
            esp_mqtt_client_subscribe(s_client, s_subs[i].topic, 1);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        reassembly_reset(); /* discard any partial payload */
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT publish ack msg_id=%d", event->msg_id);
        if (s_ack_handler != NULL) {
            s_ack_handler(event->msg_id, ESP_OK, s_ack_ctx);
        }
        break;

    case MQTT_EVENT_DATA:
        handle_data_event(event);
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

    bool certs_ok = certs_are_provisioned();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri              = cfg->broker_uri,
            .verification.certificate = certs_ok ? aws_root_ca_pem : NULL,
        },
        .credentials = {
            .client_id = cfg->client_id,
            .authentication = {
                .certificate = certs_ok ? aws_device_cert_pem        : NULL,
                .key         = certs_ok ? aws_device_private_key_pem : NULL,
            },
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

    ESP_LOGI(TAG, "MQTT client initialised (TLS=%s)", certs_ok ? "yes" : "no");
    return ESP_OK;
}

void mqtt_client_start(void)
{
    if (s_client != NULL) {
        esp_mqtt_client_start(s_client);
    }
}

void mqtt_client_stop(void)
{
    if (s_client != NULL) {
        s_connected = false;
        esp_mqtt_client_stop(s_client);
    }
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

message_subscribe_fn mqtt_client_get_subscribe_fn(void)
{
    return mqtt_subscribe_impl;
}
