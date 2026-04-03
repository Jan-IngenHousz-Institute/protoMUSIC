#include "device_commands.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "ambit_protocol.h"
#include "cJSON.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "dev_cmd"

static device_commands_config_t s_cfg;
static bool s_initialized = false;
static char s_mac_str[18]; /* "XX:XX:XX:XX:XX:XX\0" */

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

    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(s_mac_str, sizeof(s_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        s_mac_str[0] = '\0';
    }

    if (s_cfg.set_publish_ack_handler != NULL) {
        s_cfg.set_publish_ack_handler(on_publish_ack, NULL);
    }

    ESP_LOGI(TAG, "Device commands initialized");
    ESP_LOGI(TAG, "  MAC:            %s", s_mac_str[0] ? s_mac_str : "(unavail)");
    ESP_LOGI(TAG, "  topic_root:     %s", s_cfg.topic_root      ? s_cfg.topic_root      : "(null)");
    ESP_LOGI(TAG, "  device_id:      %s", s_cfg.device_id       ? s_cfg.device_id       : "(null)");
    ESP_LOGI(TAG, "  protocol_id:    %s", s_cfg.protocol_id     ? s_cfg.protocol_id     : "(null)");
    ESP_LOGI(TAG, "  device_name:    %s", s_cfg.device_name     ? s_cfg.device_name     : "(null)");
    ESP_LOGI(TAG, "  device_version: %s", s_cfg.device_version  ? s_cfg.device_version  : "(null)");
    ESP_LOGI(TAG, "  device_firm:    %s", s_cfg.device_firmware ? s_cfg.device_firmware : "(null)");
    ESP_LOGI(TAG, "  firmware_ver:   %s", s_cfg.firmware_version? s_cfg.firmware_version: "(null)");
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

cmd_result_t cmd_mqtt_publish_raw(const char *payload)
{
    if (!s_initialized || s_cfg.publish == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT not available");
    }
    if (payload == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "payload required");
    }
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s/cli",
             s_cfg.topic_root ? s_cfg.topic_root : "",
             s_cfg.device_id  ? s_cfg.device_id  : "");
    ESP_LOGI(TAG, "mqtt_pub → topic: %s", topic);
    ESP_LOGI(TAG, "mqtt_pub → payload: %s", payload);
    int msg_id = 0;
    esp_err_t err = s_cfg.publish(topic, payload, strlen(payload), &msg_id);
    if (err != ESP_OK) {
        return make_result(err, "publish failed: %s", esp_err_to_name(err));
    }
    return make_result(ESP_OK, "published to %s (msg_id=%d)", topic, msg_id);
}

/* ── helpers shared by the two publish commands ──────────────────── */

#define MQTT_TOPIC_MAX   256
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

    /* Build set[] array: [{"key":"<data_type>","value":<float>}, …] */
    char set_buf[512];
    int sp = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && sp < (int)sizeof(set_buf) - 3) {
            set_buf[sp++] = ',';
        }
        int n = snprintf(set_buf + sp, sizeof(set_buf) - (size_t)sp,
                         "{\"key\":\"%s\",\"value\":%.6g}",
                         records[i].data_type, (double)records[i].value);
        if (n > 0 && sp + n < (int)sizeof(set_buf)) {
            sp += n;
        } else {
            break;
        }
    }
    set_buf[sp] = '\0';

    /* Build inner sample JSON (unescaped) */
    char sample_inner[600];
    int si = snprintf(sample_inner, sizeof(sample_inner),
                      "{\"protocol_id\":\"%s\",\"set\":[%s]}",
                      s_cfg.protocol_id ? s_cfg.protocol_id : "",
                      set_buf);

    /* JSON-encode sample_inner as a string value: escape " → \" */
    char sample_esc[800];
    int se = 0;
    for (int j = 0; j < si && se < (int)sizeof(sample_esc) - 2; j++) {
        if (sample_inner[j] == '"' && se < (int)sizeof(sample_esc) - 3) {
            sample_esc[se++] = '\\';
        }
        sample_esc[se++] = sample_inner[j];
    }
    sample_esc[se] = '\0';

    /* ISO 8601 timestamp */
    char ts_str[32];
    time_t ts = (time_t)records[0].timestamp;
    struct tm tm_info;
    gmtime_r(&ts, &tm_info);
    strftime(ts_str, sizeof(ts_str), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    /* Build full payload — sample is a JSON-encoded string per server schema */
    char payload[MQTT_PAYLOAD_MAX];
    int pos = snprintf(payload, sizeof(payload),
                       "{\"sample\":\"%s\","
                       "\"device_firmware\":\"%s\","
                       "\"device_id\":\"%s\","
                       "\"device_name\":\"%s\","
                       "\"device_version\":\"%s\","
                       "\"firmware_version\":\"%s\","
                       "\"timestamp\":\"%s\"}",
                       sample_esc,
                       s_cfg.device_firmware  ? s_cfg.device_firmware  : "",
                       s_mac_str,
                       s_cfg.device_name      ? s_cfg.device_name      : "",
                       s_cfg.device_version   ? s_cfg.device_version   : "",
                       s_cfg.firmware_version ? s_cfg.firmware_version : "",
                       ts_str);

    ESP_LOGI(TAG, "publish → topic: %s", topic);
    ESP_LOGI(TAG, "publish → payload: %.120s%s", payload, pos > 120 ? "…" : "");

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

/* ── UART sensor commands — raw interface (Phase 7) ─────────────── */

/* Generic binary query: sends the 8-byte `cmd` (+ optional `extra` payload)
 * to an AMBIT sensor on `channel` (0-3) using the ambit-1 ESP protocol.
 *
 * `expect_raw` selects the response mode:
 *   UART_QUERY_ACK_ONLY  — return after CMD_DONE (0xA1); no response data, no CMD_END
 *   0                    — FSM data-transfer mode: wait for structured array data via
 *                          handshake (for measurement commands 20, 21)
 *   >0                   — immediate raw response: read exactly N bytes after CMD_DONE,
 *                          then verify CMD_END (0xF0) follows (for query commands 31-34)
 *
 * Caller must free response via uart_sensor_response_free().
 * Lua: device.uart_query(ch, {cmd_bytes}, extra_or_nil, expect_raw, timeout_ms)
 * CLI: not exposed directly (use typed ambit_* commands instead)                      */
cmd_result_t cmd_uart_query(uint8_t channel, const uint8_t cmd[8],
                            const uint8_t *extra, size_t extra_len,
                            size_t expect_raw,
                            uart_sensor_response_t *response,
                            uint32_t timeout_ms)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    if (channel >= UART_SENSOR_NUM_CHANNELS) {
        return make_result(ESP_ERR_INVALID_ARG, "invalid channel %u", channel);
    }
    esp_err_t err = s_cfg.uart_query(channel, cmd, extra, extra_len,
                                     expect_raw, response, timeout_ms);
    if (err != ESP_OK) {
        return make_result(err, "UART ch%u query failed: %s",
                           channel, esp_err_to_name(err));
    }
    if (expect_raw > 0) {
        return make_result(ESP_OK, "UART ch%u: %u raw bytes",
                           channel, (unsigned)response->raw_len);
    }
    return make_result(ESP_OK, "UART ch%u: %u arrays received",
                       channel, response->array_count);
}

/* Ping: send wake byte (0xAA) to the AMBIT sensor, wait for ack (0x80).
 * Result is cached for 10 seconds to avoid hammering the bus.
 * Lua:  device.uart_ping(ch) → true/false
 * CLI:  ping_uart <ch>                                                */
cmd_result_t cmd_uart_ping(uint8_t channel, bool *connected)
{
    if (!s_initialized || s_cfg.uart_ping == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    if (channel >= UART_SENSOR_NUM_CHANNELS) {
        return make_result(ESP_ERR_INVALID_ARG, "invalid channel %u", channel);
    }
    esp_err_t err = s_cfg.uart_ping(channel, connected);
    if (err != ESP_OK) {
        return make_result(err, "UART ch%u ping failed: %s",
                           channel, esp_err_to_name(err));
    }
    return make_result(ESP_OK, "AMBIT%u: %s",
                       channel + 1, *connected ? "connected" : "disconnected");
}

/* Report connection state of all 4 UART channels.
 * Lua:  device.uart_status() → string
 * CLI:  uart_status                                                   */
cmd_result_t cmd_uart_status(void)
{
    if (!s_initialized || s_cfg.uart_status == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    static const char *state_str[] = { "disconnected", "connected", "busy" };
    char buf[200];
    int pos = 0;
    for (uint8_t ch = 0; ch < UART_SENSOR_NUM_CHANNELS; ch++) {
        uart_sensor_state_t st = UART_SENSOR_DISCONNECTED;
        s_cfg.uart_status(ch, &st);
        int n = snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                         "AMBIT%u:%s ", ch + 1,
                         (st < 3) ? state_str[st] : "?");
        if (n > 0 && pos + n < (int)sizeof(buf)) {
            pos += n;
        }
    }
    return make_result(ESP_OK, "%s", buf);
}

/* ── Typed Ambit commands ────────────────────────────────────────── *
 *
 * Each function wraps one ambit-1 ESP binary command (see ambit_protocol.h
 * for command IDs). The ambit-1 protocol is:
 *   Wake (0xAA×3) → Ack (0x80) → Header (0xA0) + 8-byte cmd [+extra]
 *   → CMD_DONE (0xA1) → [response] → [CMD_END (0xF0)]
 *
 * Four response patterns exist:
 *   ACK_ONLY  — cmds 1, 2, 10: CMD_DONE only, no CMD_END (config)
 *   RAW       — cmds 31-34:    CMD_DONE → N fixed bytes → CMD_END (query)
 *   FSM       — cmds 20, 21:   CMD_DONE → FSM handshake arrays → CMD_END (measurement)
 *   ACTION    — cmds 4-6, 17-18, 37: CMD_DONE → [work] → CMD_END (no data returned)
 * ────────────────────────────────────────────────────────────────── */

/* Helper: send an ack-only command (no response, no CMD_END) */
static cmd_result_t ambit_ack_only(uint8_t ch, const uint8_t cmd[8])
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0,
                                     UART_QUERY_ACK_ONLY, &resp, 5000);
    uart_sensor_response_free(&resp);
    if (err != ESP_OK) {
        return make_result(err, "AMBIT%u cmd %u failed: %s",
                           ch + 1, cmd[0], esp_err_to_name(err));
    }
    return make_result(ESP_OK, "AMBIT%u cmd %u OK", ch + 1, cmd[0]);
}

/* Helper: send a command that returns CMD_END but no data (action) */
static cmd_result_t ambit_action(uint8_t ch, const uint8_t cmd[8],
                                 const uint8_t *extra, size_t extra_len,
                                 uint32_t timeout_ms)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, extra, extra_len,
                                     0, &resp, timeout_ms);
    uart_sensor_response_free(&resp);
    if (err != ESP_OK) {
        return make_result(err, "AMBIT%u cmd %u failed: %s",
                           ch + 1, cmd[0], esp_err_to_name(err));
    }
    return make_result(ESP_OK, "AMBIT%u cmd %u OK", ch + 1, cmd[0]);
}

/* Cmd 1 — Set photodetector gains on the ADPD6100.
 * Values 1-6 map to gain levels (0 = skip / keep current).
 * Must be called before cmd_ambit_config_detector() or cmd_ambit_run().
 * Lua:  device.ambit_set_gains(ch, fluo, fluoref, ir, irref, sun, leaf)
 * CLI:  (not exposed — use Lua)                                       */
cmd_result_t cmd_ambit_set_gains(uint8_t ch, uint8_t fluo, uint8_t fluoref,
                                  uint8_t ir, uint8_t irref,
                                  uint8_t sun, uint8_t leaf)
{
    uint8_t cmd[8] = { AMBIT_CMD_SET_GAINS, fluo, fluoref, ir, irref, sun, leaf, 0 };
    return ambit_ack_only(ch, cmd);
}

/* Cmd 2 — Set LED drive currents (0-126).
 * i620 = 620nm pulsed, i720 = 720nm pulsed, ir = far-red DC.
 * Must be called before cmd_ambit_config_detector() or cmd_ambit_run().
 * Lua:  device.ambit_set_currents(ch, i620, i720, ir)                 */
cmd_result_t cmd_ambit_set_currents(uint8_t ch, uint8_t i620, uint8_t i720,
                                     uint8_t ir)
{
    uint8_t cmd[8] = { AMBIT_CMD_SET_CURRENTS, i620, i720, ir, 0, 0, 0, 0 };
    return ambit_ack_only(ch, cmd);
}

/* Cmd 10 — Apply stored gains and currents to the ADPD6100 detector.
 * Configures the detector into ARRAY_MODE1. Call after set_gains/set_currents,
 * or omit if cmd_ambit_run() auto-configures when mode differs.
 * Lua:  device.ambit_config_detector(ch)                              */
cmd_result_t cmd_ambit_config_detector(uint8_t ch)
{
    uint8_t cmd[8] = { AMBIT_CMD_CONFIG_DETECTOR, 0, 0, 0, 0, 0, 0, 0 };
    return ambit_ack_only(ch, cmd);
}

/* Cmd 32 — Read leaf and chip temperature from the MLX90632 IR sensor.
 * Returns temperatures in Celsius (ambit sends int16 × 10, we divide).
 * Response: 4 bytes (2 × int16_t little-endian).
 * Lua:  device.ambit_get_temp(ch) → {leaf=float, chip=float}
 * CLI:  ambit_temp <ch>                                               */
cmd_result_t cmd_ambit_get_temp(uint8_t ch, float *leaf_temp, float *chip_temp)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uint8_t cmd[8] = { AMBIT_CMD_GET_TEMP, 0, 0, 0, 0, 0, 0, 0 };
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0,
                                     AMBIT_RESP_TEMP_SIZE, &resp, 5000);
    if (err != ESP_OK || resp.raw == NULL || resp.raw_len < AMBIT_RESP_TEMP_SIZE) {
        uart_sensor_response_free(&resp);
        return make_result(err != ESP_OK ? err : ESP_FAIL,
                           "AMBIT%u get_temp failed", ch + 1);
    }
    int16_t t1, t2;
    memcpy(&t1, resp.raw + 0, 2);
    memcpy(&t2, resp.raw + 2, 2);
    if (leaf_temp) *leaf_temp = (float)t1 / 10.0f;
    if (chip_temp) *chip_temp = (float)t2 / 10.0f;
    uart_sensor_response_free(&resp);
    return make_result(ESP_OK, "AMBIT%u T=%.1fC chip=%.1fC",
                       ch + 1, (float)t1 / 10.0f, (float)t2 / 10.0f);
}

/* Cmd 31 — Read spectral channels from the AS7341 and compute PAR.
 * Response: 24 bytes = 10 × uint16 channels + 1 × float32 PAR.
 * Lua:  device.ambit_get_spec(ch) → {spec={10 ints}, par=float}
 * CLI:  ambit_spec <ch>                                               */
cmd_result_t cmd_ambit_get_spec(uint8_t ch, uint16_t spec[10], float *par)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uint8_t cmd[8] = { AMBIT_CMD_GET_SPEC, 0, 0, 0, 0, 0, 0, 0 };
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0,
                                     AMBIT_RESP_SPEC_SIZE, &resp, 5000);
    if (err != ESP_OK || resp.raw == NULL || resp.raw_len < AMBIT_RESP_SPEC_SIZE) {
        uart_sensor_response_free(&resp);
        return make_result(err != ESP_OK ? err : ESP_FAIL,
                           "AMBIT%u get_spec failed", ch + 1);
    }
    /* spec[0..9] are uint16 channels, spec[10..11] hold a float (PAR) */
    if (spec) memcpy(spec, resp.raw, 20);
    if (par)  memcpy(par, resp.raw + 20, 4);
    uart_sensor_response_free(&resp);
    return make_result(ESP_OK, "AMBIT%u spectrum OK", ch + 1);
}

/* Cmd 34 — Extended temperature read: two leaf algorithms + chip + 4 raw
 * MLX90632 register values for diagnostics.
 * Response: 14 bytes (7 × int16_t: leaf*10, leaf1*10, chip*10, a1..a4).
 * Lua:  device.ambit_get_temp_raw(ch) → {leaf,leaf1,chip,raw={4 ints}}
 * CLI:  (not exposed — use Lua or ambit_temp for basic reading)       */
cmd_result_t cmd_ambit_get_temp_raw(uint8_t ch, float *leaf, float *leaf1,
                                     float *chip, int16_t raw[4])
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uint8_t cmd[8] = { AMBIT_CMD_GET_TEMP_RAW, 0, 0, 0, 0, 0, 0, 0 };
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0,
                                     AMBIT_RESP_TEMP_RAW_SIZE, &resp, 5000);
    if (err != ESP_OK || resp.raw == NULL || resp.raw_len < AMBIT_RESP_TEMP_RAW_SIZE) {
        uart_sensor_response_free(&resp);
        return make_result(err != ESP_OK ? err : ESP_FAIL,
                           "AMBIT%u get_temp_raw failed", ch + 1);
    }
    int16_t vals[7];
    memcpy(vals, resp.raw, 14);
    if (leaf)  *leaf  = (float)vals[0] / 10.0f;
    if (leaf1) *leaf1 = (float)vals[1] / 10.0f;
    if (chip)  *chip  = (float)vals[2] / 10.0f;
    if (raw) {
        raw[0] = vals[3]; raw[1] = vals[4];
        raw[2] = vals[5]; raw[3] = vals[6];
    }
    uart_sensor_response_free(&resp);
    return make_result(ESP_OK, "AMBIT%u T=%.1f/%.1f/%.1fC",
                       ch + 1, (float)vals[0]/10.f, (float)vals[1]/10.f, (float)vals[2]/10.f);
}

/* Cmd 33 — Retrieve sensor identity/calibration data.
 *   info_type=1: ambit_calibration_t (~136 B) — name, MLX coefficients,
 *                ADPD offsets, actinic/spec coefficients, emissivity
 *   info_type=2: ambit_fw_info_t (~48 B) — FW version, MAC, build date
 *   info_type=3: ambit_metadata_t (~248 B) — GPS, altitude, user notes
 * Raw struct bytes are copied into `out`. Struct layouts defined in
 * ambit_protocol.h (must match ambit-1 nvs1.h on ESP32 Xtensa alignment).
 * Lua:  device.ambit_get_info(ch, type) → raw bytes string
 * CLI:  ambit_info <ch> <1|2|3>                                       */
cmd_result_t cmd_ambit_get_info(uint8_t ch, uint8_t info_type,
                                 uint8_t *out, size_t out_size, size_t *out_len)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    size_t expect = 0;
    switch (info_type) {
    case AMBIT_INFO_CALIBRATION: expect = sizeof(ambit_calibration_t); break;
    case AMBIT_INFO_FW:          expect = sizeof(ambit_fw_info_t);     break;
    case AMBIT_INFO_METADATA:    expect = sizeof(ambit_metadata_t);    break;
    default:
        return make_result(ESP_ERR_INVALID_ARG, "info_type must be 1-3");
    }

    uint8_t cmd[8] = { AMBIT_CMD_GET_INFO, info_type, 0, 0, 0, 0, 0, 0 };
    uart_sensor_response_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0, expect, &resp, 5000);
    if (err != ESP_OK || resp.raw == NULL) {
        uart_sensor_response_free(&resp);
        return make_result(err != ESP_OK ? err : ESP_FAIL,
                           "AMBIT%u get_info(%u) failed", ch + 1, info_type);
    }
    size_t copy = resp.raw_len < out_size ? resp.raw_len : out_size;
    if (out && copy > 0) memcpy(out, resp.raw, copy);
    if (out_len) *out_len = resp.raw_len;
    uart_sensor_response_free(&resp);
    return make_result(ESP_OK, "AMBIT%u info(%u): %u bytes",
                       ch + 1, info_type, (unsigned)resp.raw_len);
}

/* Cmd 21 — Run an array-mode measurement on the ADPD6100.
 * `run_arr` is a flat byte array of arr_len × 8 bytes. Each 8-byte line
 * encodes: line_type, ir_on, sample_num(H), sample_num(L), freq(H),
 * freq(L), actinic, subsampling. Max 16 lines.
 * The extra payload is sent before CMD_DONE (buffered in ambit RX FIFO).
 * Response: FSM handshake — up to 7 data arrays (env, fluor, fluoref,
 * sun, leaf, 730, 730ref), each as {index, uint32_t[], length}.
 * Typical duration: seconds to ~60s depending on sample count.
 * Lua:  device.ambit_run(ch, flat_table, led_persist, allow_int, timeout)
 * CLI:  (not exposed — use Lua scripts for measurement workflows)     */
cmd_result_t cmd_ambit_run(uint8_t ch, const uint8_t *run_arr, uint8_t arr_len,
                            uint8_t led_persist, bool allow_interrupt,
                            uart_sensor_response_t *response, uint32_t timeout_ms)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    if (arr_len == 0 || arr_len > 16 || run_arr == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "arr_len must be 1-16");
    }
    uint8_t cmd[8] = { AMBIT_CMD_RUN, arr_len, led_persist,
                       (uint8_t)(allow_interrupt ? 1 : 0), 0, 0, 0, 0 };
    memset(response, 0, sizeof(*response));
    esp_err_t err = s_cfg.uart_query(ch, cmd, run_arr, (size_t)arr_len * 8,
                                     0, response, timeout_ms);
    if (err != ESP_OK) {
        return make_result(err, "AMBIT%u run failed: %s",
                           ch + 1, esp_err_to_name(err));
    }
    return make_result(ESP_OK, "AMBIT%u run: %u arrays",
                       ch + 1, response->array_count);
}

/* Cmd 20 — Run a multi-phase fluorescence (MPF) measurement.
 * `length` = total measurement points (uint16, encoded as [1]<<7 | [2]),
 * `interval` = sampling interval, `change_act`/`act` = actinic control.
 * Response: FSM handshake data arrays (same as cmd 21).
 * Lua:  device.ambit_run_mpf(ch, length, interval, change_act, act, timeout)
 * CLI:  (not exposed — use Lua)                                       */
cmd_result_t cmd_ambit_run_mpf(uint8_t ch, uint16_t length, uint8_t interval,
                                bool change_act, uint8_t act,
                                uart_sensor_response_t *response, uint32_t timeout_ms)
{
    if (!s_initialized || s_cfg.uart_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    uint8_t cmd[8] = { AMBIT_CMD_RUN_MPF,
                       (uint8_t)(length >> 7), (uint8_t)(length & 0x7F),
                       interval, (uint8_t)(change_act ? 1 : 0), act, 0, 0 };
    memset(response, 0, sizeof(*response));
    esp_err_t err = s_cfg.uart_query(ch, cmd, NULL, 0, 0, response, timeout_ms);
    if (err != ESP_OK) {
        return make_result(err, "AMBIT%u run_mpf failed: %s",
                           ch + 1, esp_err_to_name(err));
    }
    return make_result(ESP_OK, "AMBIT%u mpf: %u arrays",
                       ch + 1, response->array_count);
}

/* Cmd 5 — Blink the AS7341 LED for visual identification.
 * `ambit_id` 0-3 selects blink pattern, `intensity` 5-253 sets brightness.
 * Blocks until blink completes (a few seconds).
 * Lua:  device.ambit_blink(ch, id, intensity)
 * CLI:  ambit_blink <ch> <id> <intensity>                             */
cmd_result_t cmd_ambit_blink(uint8_t ch, uint8_t ambit_id, uint8_t intensity)
{
    uint8_t cmd[8] = { AMBIT_CMD_BLINK, ambit_id, intensity, 0, 0, 0, 0, 0 };
    return ambit_action(ch, cmd, NULL, 0, 10000);
}

/* Cmd 6 — Run ADPD6100 fluorescence offset calibration.
 * Measures dark baseline and stores offsets (adpd_lit, adpd_sun, adpd_leaf,
 * adpd_730, adpd_730r) in the ambit's NVS. Factory/maintenance command.
 * Blocks for several seconds.
 * Lua:  device.ambit_calibrate_baseline(ch)                           */
cmd_result_t cmd_ambit_calibrate_baseline(uint8_t ch)
{
    uint8_t cmd[8] = { AMBIT_CMD_CALIBRATE_BASELINE, 0, 0, 0, 0, 0, 0, 0 };
    return ambit_action(ch, cmd, NULL, 0, 30000);
}

/* Cmd 4 — Actinic LED control and calibration.
 *   type=1: test actinics — ramps LED current (var), blocks ~6s
 *   type=2: set actinic coefficient (float packed in cmd[3..6])
 *   type=4: set spectral coefficient
 *   type=5: pulse LED at `var` current for `var2`×100 ms
 * Factory/calibration command.
 * Lua:  device.ambit_actinic(ch, type, var, var2)                     */
cmd_result_t cmd_ambit_actinic(uint8_t ch, uint8_t type, uint8_t var, uint8_t var2)
{
    uint8_t cmd[8] = { AMBIT_CMD_ACTINIC, type, var, var2, 0, 0, 0, 0 };
    return ambit_action(ch, cmd, NULL, 0, 15000);
}

/* Cmd 37 — Write metadata (GPS coordinates, altitude, user notes) to
 * the ambit's NVS. `metadata` should be a serialised ambit_metadata_t
 * (~248 bytes). The struct's eof_mark must be 2025 for the ambit to
 * accept the write. Data is sent as `extra` before CMD_DONE; the ambit
 * reads it from its UART RX FIFO after acknowledging.
 * Lua:  device.ambit_set_metadata(ch, metadata_string)                */
cmd_result_t cmd_ambit_set_metadata(uint8_t ch, const uint8_t *metadata, size_t len)
{
    uint8_t cmd[8] = { AMBIT_CMD_SET_METADATA, 0, 0, 0, 0, 0, 0, 0 };
    return ambit_action(ch, cmd, metadata, len, 5000);
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

        } else if (strcmp(cmd, "uart_ping") == 0) {
            cJSON *ch_field = cJSON_GetObjectItemCaseSensitive(root, "channel");
            if (!cJSON_IsNumber(ch_field)) {
                res = make_result(ESP_ERR_INVALID_ARG, "uart_ping requires 'channel'");
            } else {
                bool conn = false;
                res = cmd_uart_ping((uint8_t)ch_field->valueint, &conn);
            }

        } else if (strcmp(cmd, "uart_status") == 0) {
            res = cmd_uart_status();

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

    char topic[MQTT_TOPIC_MAX];
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
