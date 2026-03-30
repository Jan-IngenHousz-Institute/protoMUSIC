#include "device_commands.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "cJSON.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "dev_cmd"

static device_commands_config_t s_cfg;
static bool s_initialized = false;

/* Single in-flight measurement publish slot — correlates QoS-1 ack to a measureID */
static int64_t s_inflight_measure_id = -1;
static int     s_inflight_msg_id     = -1;

static cmd_result_t make_result(esp_err_t status, const char *fmt, ...)
{
    cmd_result_t r;
    r.status = status;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r.message, sizeof(r.message), fmt, ap);
    va_end(ap);
    return r;
}

/* Internal ack handler — called by mqtt_client on MQTT_EVENT_PUBLISHED */
static void on_publish_ack(int msg_id, esp_err_t status, void *ctx)
{
    (void)ctx;
    if (s_inflight_msg_id < 0 || msg_id != s_inflight_msg_id) {
        return; /* Unknown ack — ignore (e.g. raw cmd_mqtt_publish) */
    }
    if (s_inflight_measure_id < 0) {
        return;
    }

    if (status == ESP_OK) {
        if (s_cfg.mark_synced != NULL) {
            s_cfg.mark_synced(s_inflight_measure_id);
        }
    } else {
        if (s_cfg.mark_pending != NULL) {
            s_cfg.mark_pending(s_inflight_measure_id);
        }
    }

    s_inflight_measure_id = -1;
    s_inflight_msg_id     = -1;
}

esp_err_t device_commands_init(const device_commands_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    s_initialized = true;

    if (s_cfg.set_publish_ack_handler != NULL) {
        s_cfg.set_publish_ack_handler(on_publish_ack, NULL);
    }

    ESP_LOGI(TAG, "Device commands initialized");
    return ESP_OK;
}

cmd_result_t cmd_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_initialized || s_cfg.set_status == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "status LED not available");
    }
    esp_err_t err = s_cfg.set_status(r, g, b);
    if (err != ESP_OK) {
        return make_result(err, "set_rgb failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "RGB set to (%u, %u, %u)", r, g, b);
}

cmd_result_t cmd_read_rtc(time_t *out_time)
{
    if (!s_initialized || s_cfg.read_clock == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "RTC not available");
    }
    if (out_time == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "out_time is NULL");
    }
    esp_err_t err = s_cfg.read_clock(out_time);
    if (err != ESP_OK) {
        return make_result(err, "RTC read failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "RTC: %lld", (long long)*out_time);
}

cmd_result_t cmd_device_status(bool *bme_ready, bool *rtc_ready, time_t *rtc_time)
{
    bool bme_ok = false;
    bool rtc_ok = false;

    if (s_initialized && s_cfg.read_env != NULL) {
        measurement_t m;
        bme_ok = (s_cfg.read_env(&m) == ESP_OK);
    }

    if (s_initialized && s_cfg.read_clock != NULL) {
        time_t t = 0;
        if (s_cfg.read_clock(&t) == ESP_OK) {
            rtc_ok = true;
            if (rtc_time != NULL) {
                *rtc_time = t;
            }
        }
    }

    if (bme_ready != NULL) {
        *bme_ready = bme_ok;
    }
    if (rtc_ready != NULL) {
        *rtc_ready = rtc_ok;
    }

    return make_result(ESP_OK, "BME280=%s RTC=%s",
                       bme_ok ? "ok" : "unavail",
                       rtc_ok ? "ok" : "unavail");
}

cmd_result_t cmd_read_env(float *temp, float *hum, float *pres)
{
    if (!s_initialized || s_cfg.read_env == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "env sensor not available");
    }
    measurement_t m;
    esp_err_t err = s_cfg.read_env(&m);
    if (err != ESP_OK) {
        return make_result(err, "env read failed: %s", esp_err_to_name(err));
    }
    if (temp != NULL) *temp = m.temperature_c;
    if (hum != NULL)  *hum = m.humidity_percent;
    if (pres != NULL) *pres = m.pressure_pa;
    return make_result(ESP_OK, "T=%.2fC H=%.1f%% P=%.0fPa",
                       m.temperature_c, m.humidity_percent, m.pressure_pa);
}

cmd_result_t cmd_log(const char *msg)
{
    if (msg == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "msg is NULL");
    }
    ESP_LOGI(TAG, "%s", msg);
    return make_result(ESP_OK, "logged");
}

cmd_result_t cmd_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
    return make_result(ESP_OK, "slept %lu ms", (unsigned long)ms);
}

cmd_result_t cmd_store_measurement(const measurement_record_t *records, size_t count)
{
    if (!s_initialized || s_cfg.store == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    if (records == NULL || count == 0) {
        return make_result(ESP_ERR_INVALID_ARG, "no records to store");
    }
    esp_err_t err = s_cfg.store(records, count);
    if (err != ESP_OK) {
        return make_result(err, "store failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "stored %u record(s)", (unsigned)count);
}

cmd_result_t cmd_query_measurements(const char *measure_type, time_t from, time_t to,
                                    measurement_record_t *out, size_t max, size_t *count)
{
    if (!s_initialized || s_cfg.query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    esp_err_t err = s_cfg.query(measure_type, from, to, out, max, count);
    if (err != ESP_OK) {
        return make_result(err, "query failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "query returned %u record(s)", (unsigned)*count);
}

cmd_result_t cmd_measurement_count(const char *measure_type, size_t *count)
{
    if (!s_initialized || s_cfg.count == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    esp_err_t err = s_cfg.count(measure_type, count);
    if (err != ESP_OK) {
        return make_result(err, "count failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "count: %u", (unsigned)*count);
}

cmd_result_t cmd_next_measure_id(int64_t *out_id)
{
    if (!s_initialized || s_cfg.next_id == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    esp_err_t err = s_cfg.next_id(out_id);
    if (err != ESP_OK) {
        return make_result(err, "next_id failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "next_id: %lld", (long long)*out_id);
}

cmd_result_t cmd_query_unsynced(const char *measure_type, measurement_record_t *out,
                                size_t max, size_t *count)
{
    if (!s_initialized || s_cfg.query_unsynced == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    esp_err_t err = s_cfg.query_unsynced(measure_type, out, max, count);
    if (err != ESP_OK) {
        return make_result(err, "query_unsynced failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "unsynced: %u record(s)", (unsigned)*count);
}

cmd_result_t cmd_mqtt_status(void)
{
    if (!s_initialized || s_cfg.message_is_connected == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT not available");
    }
    bool connected = s_cfg.message_is_connected();
    return make_result(ESP_OK, "MQTT: %s", connected ? "connected" : "disconnected");
}

cmd_result_t cmd_mqtt_publish(const char *topic, const char *payload)
{
    if (!s_initialized || s_cfg.publish == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT not available");
    }
    if (topic == NULL || payload == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "topic and payload required");
    }
    int msg_id = 0;
    esp_err_t err = s_cfg.publish(topic, payload, strlen(payload), &msg_id);
    if (err != ESP_OK) {
        return make_result(err, "publish failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "published (msg_id=%d)", msg_id);
}

/* ── helpers shared by the two publish commands ──────────────────── */

#define MQTT_TOPIC_MAX   128
#define MQTT_PAYLOAD_MAX 1024
#define MQTT_RECORDS_MAX 8

static cmd_result_t build_and_publish(int64_t measure_id)
{
    measurement_record_t records[MQTT_RECORDS_MAX];
    size_t count = 0;
    esp_err_t err = s_cfg.query_by_id(measure_id, records, MQTT_RECORDS_MAX, &count);
    if (err != ESP_OK || count == 0) {
        s_cfg.mark_pending(measure_id);
        return make_result(err != ESP_OK ? err : ESP_ERR_NOT_FOUND,
                           "query_by_id failed for measure_id=%lld", (long long)measure_id);
    }

    /* Build topic: <root>/<device_id>/<measure_type>/<measure_id> */
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%lld",
             s_cfg.topic_root ? s_cfg.topic_root : "",
             s_cfg.device_id  ? s_cfg.device_id  : "",
             records[0].measure_type,
             (long long)measure_id);

    /* Build JSON: {"measure_id":…,"sensor_id":…,"measure_type":…,"timestamp":…,"values":{…}} */
    char payload[MQTT_PAYLOAD_MAX];
    int pos = snprintf(payload, sizeof(payload),
                       "{\"measure_id\":%lld,\"sensor_id\":%lld,\"measure_type\":\"%s\","
                       "\"timestamp\":%lld,\"values\":{",
                       (long long)records[0].measure_id,
                       (long long)records[0].sensor_id,
                       records[0].measure_type,
                       (long long)records[0].timestamp);

    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            if (pos + 1 >= (int)sizeof(payload) - 3) break;
            payload[pos++] = ',';
        }
        /* Use actual written length, not would-be length, to avoid advancing pos past buffer */
        int remaining = (int)sizeof(payload) - pos - 3; /* reserve room for "}}\0" */
        if (remaining <= 0) break;
        int n = snprintf(payload + pos, (size_t)remaining,
                         "\"%s\":%.6g", records[i].data_type, (double)records[i].value);
        if (n > 0 && n < remaining) {
            pos += n;
        } else {
            break; /* truncated — stop */
        }
    }
    payload[pos++] = '}';
    payload[pos++] = '}';
    payload[pos]   = '\0';

    int msg_id = 0;
    err = s_cfg.publish(topic, payload, (size_t)pos, &msg_id);
    if (err != ESP_OK) {
        s_cfg.mark_pending(measure_id);
        return make_result(err, "publish failed: %s", esp_err_to_name(err));
    }

    s_inflight_measure_id = measure_id;
    s_inflight_msg_id     = msg_id;
    return make_result(ESP_OK, "published measure_id=%lld msg_id=%d",
                       (long long)measure_id, msg_id);
}

cmd_result_t cmd_mqtt_publish_measurement(int64_t measure_id)
{
    if (!s_initialized || s_cfg.publish == NULL ||
        s_cfg.query_by_id == NULL || s_cfg.mark_inflight == NULL ||
        s_cfg.mark_pending == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT or persistence not available");
    }
    if (s_inflight_measure_id >= 0) {
        return make_result(ESP_ERR_INVALID_STATE,
                           "measure_id %lld already in flight",
                           (long long)s_inflight_measure_id);
    }

    esp_err_t err = s_cfg.mark_inflight(measure_id);
    if (err != ESP_OK) {
        return make_result(err, "mark_inflight failed: %s", esp_err_to_name(err));
    }

    return build_and_publish(measure_id);
}

cmd_result_t cmd_mqtt_publish_unsynced(const char *measure_type)
{
    if (!s_initialized || s_cfg.publish == NULL ||
        s_cfg.claim_next_pending == NULL || s_cfg.query_by_id == NULL ||
        s_cfg.mark_pending == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT or persistence not available");
    }
    if (s_inflight_measure_id >= 0) {
        return make_result(ESP_ERR_INVALID_STATE,
                           "measure_id %lld already in flight",
                           (long long)s_inflight_measure_id);
    }

    /* Atomically claim the oldest PENDING group (marks it INFLIGHT in DB) */
    int64_t claimed_id = -1;
    esp_err_t err = s_cfg.claim_next_pending(measure_type, &claimed_id);
    if (err == ESP_ERR_NOT_FOUND) {
        return make_result(ESP_ERR_NOT_FOUND, "no pending measurements for '%s'",
                           measure_type);
    }
    if (err != ESP_OK) {
        return make_result(err, "claim_next_pending failed: %s", esp_err_to_name(err));
    }

    return build_and_publish(claimed_id);
}

void device_commands_on_mqtt_disconnect(void)
{
    if (s_inflight_measure_id >= 0) {
        if (s_cfg.mark_pending != NULL) {
            s_cfg.mark_pending(s_inflight_measure_id);
        }
        s_inflight_measure_id = -1;
        s_inflight_msg_id     = -1;
    }
}

cmd_result_t cmd_cert_status(void)
{
    if (!s_initialized || s_cfg.certs_status == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "cert status not available");
    }
    bool ok = s_cfg.certs_status();
    return make_result(ESP_OK, "certs: %s", ok ? "provisioned" : "not provisioned");
}

/* ── inbound command dispatch ────────────────────────────────────── */

cmd_result_t cmd_dispatch_json(const char *json, size_t len)
{
    (void)len;
    cmd_result_t res = make_result(ESP_ERR_INVALID_ARG, "no command executed");

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "JSON parse error");
    }

    cJSON *cmd_field = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_field) || cmd_field->valuestring == NULL) {
        res = make_result(ESP_ERR_INVALID_ARG, "missing or invalid 'cmd' field");
        goto done;
    }

    {
        const char *cmd = cmd_field->valuestring;

        if (strcmp(cmd, "set_rgb") == 0) {
            cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "r");
            cJSON *g = cJSON_GetObjectItemCaseSensitive(root, "g");
            cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "b");
            if (!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b)) {
                res = make_result(ESP_ERR_INVALID_ARG, "set_rgb requires r,g,b");
            } else {
                res = cmd_set_rgb((uint8_t)r->valueint,
                                  (uint8_t)g->valueint,
                                  (uint8_t)b->valueint);
            }

        } else if (strcmp(cmd, "read_env") == 0) {
            float t = 0, h = 0, p = 0;
            res = cmd_read_env(&t, &h, &p);

        } else if (strcmp(cmd, "status") == 0) {
            bool bme = false, rtc = false;
            time_t ts = 0;
            res = cmd_device_status(&bme, &rtc, &ts);

        } else if (strcmp(cmd, "publish_unsynced") == 0) {
            cJSON *type_field = cJSON_GetObjectItemCaseSensitive(root, "type");
            if (!cJSON_IsString(type_field) || type_field->valuestring == NULL) {
                res = make_result(ESP_ERR_INVALID_ARG, "publish_unsynced requires 'type'");
            } else {
                res = cmd_mqtt_publish_unsynced(type_field->valuestring);
            }

        } else if (strcmp(cmd, "mqtt_status") == 0) {
            res = cmd_mqtt_status();

        } else if (strcmp(cmd, "cert_status") == 0) {
            res = cmd_cert_status();

        } else if (strcmp(cmd, "sleep_ms") == 0) {
            cJSON *ms_field = cJSON_GetObjectItemCaseSensitive(root, "ms");
            if (!cJSON_IsNumber(ms_field)) {
                res = make_result(ESP_ERR_INVALID_ARG, "sleep_ms requires 'ms'");
            } else {
                res = cmd_sleep_ms((uint32_t)ms_field->valueint);
            }

        } else if (strcmp(cmd, "log") == 0) {
            cJSON *msg_field = cJSON_GetObjectItemCaseSensitive(root, "msg");
            if (!cJSON_IsString(msg_field) || msg_field->valuestring == NULL) {
                res = make_result(ESP_ERR_INVALID_ARG, "log requires 'msg'");
            } else {
                res = cmd_log(msg_field->valuestring);
            }

        } else {
            res = make_result(ESP_ERR_INVALID_ARG, "unknown command: %.63s", cmd);
        }
    }

done:
    cJSON_Delete(root);
    return res;
}

static void on_inbound_command(const char *topic, const char *payload, size_t len)
{
    (void)topic;
    cmd_result_t res = cmd_dispatch_json(payload, len);
    ESP_LOGI(TAG, "inbound cmd: %s", res.message);
}

void device_commands_subscribe_inbound(void)
{
    if (!s_initialized || s_cfg.subscribe == NULL) {
        ESP_LOGW(TAG, "subscribe port not available — skipping inbound command topic");
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/cmd",
             s_cfg.topic_root ? s_cfg.topic_root : "",
             s_cfg.device_id  ? s_cfg.device_id  : "");

    esp_err_t err = s_cfg.subscribe(topic, on_inbound_command);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "subscribe failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "subscribed to inbound command topic: %s", topic);
    }
}
