#include "device_commands.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "ambit_protocol.h"
#include "cJSON.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "dev_cmd"

#define MQTT_TOPIC_MAX        256
#define MQTT_PAYLOAD_MAX     (128U * 1024U)   /* AWS IoT max payload */

static device_commands_config_t s_cfg;
static bool s_initialized = false;
static char s_mac_str[18]; /* "XX:XX:XX:XX:XX:XX\0" */

/* In-flight publish tracking. The sync runner claims a batch of measurement
 * groups, the MQTT layer publishes them as one message, and the broker's
 * PUBACK matches by msg_id back to the listed measure_ids. The mutex
 * serialises read/mutate access across the Lua task, sync runner, and the
 * MQTT event task that fires on_publish_ack. */
static SemaphoreHandle_t s_inflight_mtx = NULL;
static StaticSemaphore_t s_inflight_mtx_storage;
/* One event in flight at a time (one measure_id = one MQTT message). */
static int64_t s_inflight_measure_id = -1;
static int     s_inflight_msg_id     = -1;

/* Measurement-activity gate. Incremented around every UART sensor transaction
 * so the background sync runner can pause publishing while a measurement is in
 * progress (it drains during idle/sleep instead). A counter (not a bool) so
 * overlapping reads don't clear it early. Single 32-bit access is atomic on
 * the ESP32; the sync runner only reads it. */
static volatile int s_measurement_active = 0;

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
    if (s_inflight_mtx == NULL) return;
    if (xSemaphoreTake(s_inflight_mtx, pdMS_TO_TICKS(2000)) != pdTRUE) return;

    if (s_inflight_msg_id < 0 || msg_id != s_inflight_msg_id) {
        xSemaphoreGive(s_inflight_mtx);
        return; /* Unknown ack — ignore (e.g. raw cmd_mqtt_publish) */
    }
    int64_t mid = s_inflight_measure_id;
    s_inflight_measure_id = -1;
    s_inflight_msg_id     = -1;
    xSemaphoreGive(s_inflight_mtx);

    if (status == ESP_OK && s_cfg.mark_event_synced != NULL) {
        s_cfg.mark_event_synced(mid);
    } else if (s_cfg.mark_event_pending != NULL) {
        s_cfg.mark_event_pending(mid);
    }
}

esp_err_t device_commands_init(const device_commands_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    s_initialized = true;

    if (s_inflight_mtx == NULL) {
        s_inflight_mtx = xSemaphoreCreateMutexStatic(&s_inflight_mtx_storage);
    }

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

/* ── PWM output (LEDC on GPIO4) ─────────────────────────────────────────
 *
 * A single LEDC timer/channel drives GPIO4. The duty resolution is chosen per
 * call as the largest that supports the requested frequency on the 80 MHz APB
 * clock (ESP32-S3 LEDC tops out at 14-bit). */
#define PWM_GPIO          4
#define PWM_TIMER         LEDC_TIMER_0
#define PWM_CHANNEL       LEDC_CHANNEL_0
#define PWM_SPEED_MODE    LEDC_LOW_SPEED_MODE   /* S3 has no high-speed mode */
#define PWM_SRC_CLK_HZ    80000000u             /* APB clock */
#define PWM_MAX_RES_BITS  14                    /* SOC_LEDC_TIMER_BIT_WIDTH (S3) */

static bool s_pwm_configured = false;

/* Largest duty resolution (bits) whose full-scale period fits freq_hz on the
 * APB clock. Returns 0 if the frequency is too high to represent. */
static int pwm_pick_resolution(uint32_t freq_hz)
{
    int bits = 0;
    while (bits < PWM_MAX_RES_BITS &&
           (1u << (bits + 1)) <= (PWM_SRC_CLK_HZ / freq_hz)) {
        bits++;
    }
    return bits;
}

cmd_result_t cmd_pwm(float duty_pct, uint32_t freq_hz, bool enable)
{
    if (duty_pct < 0.0f || duty_pct > 100.0f) {
        return make_result(ESP_ERR_INVALID_ARG,
                           "duty must be 0..100 (got %.2f)", (double)duty_pct);
    }

    if (!enable) {
        if (s_pwm_configured) {
            ledc_stop(PWM_SPEED_MODE, PWM_CHANNEL, 0);  /* hold pin low */
        }
        return make_result(ESP_OK, "PWM disabled on GPIO%d", PWM_GPIO);
    }

    if (freq_hz == 0) {
        return make_result(ESP_ERR_INVALID_ARG, "freq must be > 0");
    }
    int bits = pwm_pick_resolution(freq_hz);
    if (bits == 0) {
        return make_result(ESP_ERR_INVALID_ARG, "freq %u Hz too high (max %u Hz)",
                           (unsigned)freq_hz, (unsigned)(PWM_SRC_CLK_HZ / 2));
    }

    ledc_timer_config_t tcfg = {
        .speed_mode      = PWM_SPEED_MODE,
        .timer_num       = PWM_TIMER,
        .duty_resolution = (ledc_timer_bit_t)bits,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_USE_APB_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return make_result(err, "ledc_timer_config(%u Hz, %d-bit) failed: %s",
                           (unsigned)freq_hz, bits, esp_err_to_name(err));
    }

    uint32_t full = 1u << bits;
    uint32_t raw  = (uint32_t)lroundf(duty_pct / 100.0f * (float)full);
    if (raw > full) {
        raw = full;
    }

    /* Re-running channel_config each enable is idempotent and re-asserts the
     * output after a prior disable (ledc_stop). */
    ledc_channel_config_t ccfg = {
        .gpio_num   = PWM_GPIO,
        .speed_mode = PWM_SPEED_MODE,
        .channel    = PWM_CHANNEL,
        .timer_sel  = PWM_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = raw,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        return make_result(err, "ledc_channel_config failed: %s",
                           esp_err_to_name(err));
    }
    s_pwm_configured = true;

    return make_result(ESP_OK, "PWM GPIO%d: %.2f%% @ %u Hz (%d-bit)",
                       PWM_GPIO, (double)duty_pct, (unsigned)freq_hz, bits);
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

/* Return current UTC milliseconds since epoch, sourced from the IDF's internal
 * clock (settimeofday'd from the RTC at boot — see components/pcf2131tfy_rtc).
 * Cheap: one syscall, no I2C transactions. */
static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

cmd_result_t cmd_record_env(int64_t *out_measure_id)
{
    if (!s_initialized || s_cfg.read_env == NULL ||
        s_cfg.store_event == NULL || s_cfg.next_id == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "env/persistence not available");
    }

    int64_t start_ms = now_ms();
    measurement_t m;
    esp_err_t err = s_cfg.read_env(&m);
    int64_t end_ms = now_ms();
    if (err != ESP_OK) {
        return make_result(err, "env read failed: %s", esp_err_to_name(err));
    }

    int64_t mid = 0;
    if ((err = s_cfg.next_id(&mid)) != ESP_OK) {
        return make_result(err, "next_id failed: %s", esp_err_to_name(err));
    }

    /* One event: T/H/P together in the payload. device "" = onboard sensor. */
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.1f}",
             m.temperature_c, m.humidity_percent, m.pressure_pa);

    err = s_cfg.store_event(mid, "", "BME280", start_ms, end_ms, NULL, payload);
    if (err != ESP_OK) {
        return make_result(err, "store failed: %s", esp_err_to_name(err));
    }

    if (out_measure_id) *out_measure_id = mid;
    return make_result(ESP_OK,
                       "recorded env id=%lld: T=%.2fC H=%.1f%% P=%.0fPa",
                       (long long)mid, m.temperature_c,
                       m.humidity_percent, m.pressure_pa);
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

cmd_result_t cmd_sd_ready(bool *out_ready)
{
    if (out_ready == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "out_ready is NULL");
    }
    if (!s_initialized || s_cfg.sd_ready == NULL) {
        *out_ready = false;
        return make_result(ESP_ERR_NOT_SUPPORTED, "SD port not wired");
    }
    *out_ready = s_cfg.sd_ready();
    return make_result(ESP_OK, "SD: %s", *out_ready ? "ready" : "out");
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

cmd_result_t cmd_mqtt_status(void)
{
    if (!s_initialized || s_cfg.message_is_connected == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT not available");
    }
    bool connected = s_cfg.message_is_connected();
    return make_result(ESP_OK, "MQTT: %s", connected ? "connected" : "disconnected");
}

cmd_result_t cmd_db_status(bool *available, int64_t *total,
                           int64_t *pending, int64_t *next_id)
{
    if (!s_initialized || s_cfg.db_stats == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "persistence not available");
    }
    bool    avail = false;
    int64_t tot = 0, pend = 0, nid = 0;
    esp_err_t err = s_cfg.db_stats(&avail, &tot, &pend, &nid);
    if (err != ESP_OK) {
        return make_result(err, "db stats failed: %s", esp_err_to_name(err));
    }
    if (available) *available = avail;
    if (total)     *total     = tot;
    if (pending)   *pending   = pend;
    if (next_id)   *next_id   = nid;
    return make_result(ESP_OK, "DB=%s total=%lld pending=%lld next_id=%lld",
                       avail ? "online" : "offline",
                       (long long)tot, (long long)pend, (long long)nid);
}

/* ── Event store + publish (one row/event; one message per measure_id) ── */

cmd_result_t cmd_store_event(int64_t measure_id, const char *device, const char *sensor,
                             int64_t start_ms, int64_t end_ms,
                             const char *metadata_json, const char *payload_json)
{
    if (!s_initialized || s_cfg.store_event == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "event storage not available (no SD?)");
    }
    if (sensor == NULL || payload_json == NULL) {
        return make_result(ESP_ERR_INVALID_ARG, "store_event: null arg");
    }
    esp_err_t err = s_cfg.store_event(measure_id, device, sensor, start_ms, end_ms,
                                      metadata_json, payload_json);
    if (err != ESP_OK) {
        return make_result(err, "store_event(%s) failed: %s", sensor, esp_err_to_name(err));
    }
    return make_result(ESP_OK, "stored event id=%lld (%s)", (long long)measure_id, sensor);
}

/* Publish the next pending event as one MQTT message (one measure_id = one
 * message). Keeps the cloud's `sample:[…]` wrapper; the event's quantities are
 * nested under `data`. */
cmd_result_t cmd_mqtt_publish_next_event(void)
{
    if (!s_initialized || s_cfg.publish == NULL ||
        s_cfg.claim_next_event == NULL || s_cfg.mark_event_pending == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT or persistence not available");
    }
    /* Don't even claim an event if the broker isn't connected — Wi-Fi/MQTT
     * may be down and we'd just shove bytes at a dead pipe (the esp-mqtt
     * client silently enqueues / drops them). Keeps events PENDING for the
     * next cycle and lets sync_runner_drain exit silently. */
    if (s_cfg.message_is_connected != NULL && !s_cfg.message_is_connected()) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "MQTT not connected");
    }
    if (s_inflight_mtx == NULL) {
        return make_result(ESP_ERR_INVALID_STATE, "inflight mutex not initialised");
    }

    /* One event in flight at a time. */
    if (xSemaphoreTake(s_inflight_mtx, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return make_result(ESP_ERR_TIMEOUT, "inflight mutex busy");
    }
    bool busy = (s_inflight_msg_id >= 0);
    xSemaphoreGive(s_inflight_mtx);
    if (busy) {
        return make_result(ESP_ERR_INVALID_STATE, "previous event still in flight");
    }

    measurement_event_t e;
    esp_err_t err = s_cfg.claim_next_event(&e);
    if (err == ESP_ERR_NOT_FOUND) {
        return make_result(ESP_ERR_NOT_FOUND, "no pending measurements");
    }
    if (err != ESP_OK) {
        return make_result(err, "claim_next_event failed: %s", esp_err_to_name(err));
    }

    cJSON *root   = cJSON_CreateObject();
    cJSON *sample = cJSON_CreateArray();
    cJSON *data   = cJSON_Parse(e.payload_json); /* payload is a JSON object */
    if (root == NULL || sample == NULL || data == NULL) {
        if (root)   cJSON_Delete(root);
        if (sample) cJSON_Delete(sample);
        if (data)   cJSON_Delete(data);
        s_cfg.mark_event_pending(e.measure_id);
        measurement_event_free(&e);
        return make_result(ESP_ERR_NO_MEM, "cJSON build failed");
    }
    cJSON_AddItemToObject(root, "sample", sample);
    cJSON_AddStringToObject(root, "device_firmware",  s_cfg.device_firmware  ? s_cfg.device_firmware  : "");
    cJSON_AddStringToObject(root, "device_id",        s_mac_str);
    cJSON_AddStringToObject(root, "device_name",      s_cfg.device_name      ? s_cfg.device_name      : "");
    cJSON_AddStringToObject(root, "device_version",   s_cfg.device_version   ? s_cfg.device_version   : "");
    cJSON_AddStringToObject(root, "firmware_version", s_cfg.firmware_version ? s_cfg.firmware_version : "");

    char ts_str[32];
    time_t ts = time(NULL);
    struct tm tm_info;
    gmtime_r(&ts, &tm_info);
    strftime(ts_str, sizeof(ts_str), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    cJSON_AddStringToObject(root, "timestamp", ts_str);

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "measure_id", (double)e.measure_id);
    cJSON_AddNumberToObject(obj, "startTicks", (double)e.start_ticks_ms);
    cJSON_AddNumberToObject(obj, "endTicks",   (double)e.end_ticks_ms);
    if (e.device[0] == '\0') cJSON_AddNullToObject(obj, "device");
    else                     cJSON_AddStringToObject(obj, "device", e.device);
    cJSON_AddStringToObject(obj, "sensor", e.sensor);
    if (e.metadata_json == NULL) {
        cJSON_AddNullToObject(obj, "metadata");
    } else {
        cJSON *meta = cJSON_Parse(e.metadata_json);
        if (meta) cJSON_AddItemToObject(obj, "metadata", meta);
        else      cJSON_AddStringToObject(obj, "metadata", e.metadata_json);
    }
    cJSON_AddItemToObject(obj, "data", data); /* root owns data */
    cJSON_AddItemToArray(sample, obj);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        s_cfg.mark_event_pending(e.measure_id);
        measurement_event_free(&e);
        return make_result(ESP_ERR_NO_MEM, "cJSON print failed");
    }

    size_t payload_len = strlen(payload);
    if (payload_len >= MQTT_PAYLOAD_MAX) {
        ESP_LOGW(TAG, "event payload %u bytes exceeds %u; may be rejected",
                 (unsigned)payload_len, (unsigned)MQTT_PAYLOAD_MAX);
    }
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof(topic), "%s/1234", s_cfg.topic_root ? s_cfg.topic_root : "");

    ESP_LOGI(TAG, "publish event -> %s (id=%lld, sensor=%s, %u bytes)",
             topic, (long long)e.measure_id, e.sensor, (unsigned)payload_len);

    int msg_id = 0;
    err = s_cfg.publish(topic, payload, payload_len, &msg_id);
    free(payload);
    if (err != ESP_OK) {
        s_cfg.mark_event_pending(e.measure_id);
        measurement_event_free(&e);
        return make_result(err, "event publish failed: %s", esp_err_to_name(err));
    }

    if (xSemaphoreTake(s_inflight_mtx, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_inflight_measure_id = e.measure_id;
        s_inflight_msg_id     = msg_id;
        xSemaphoreGive(s_inflight_mtx);
    } else {
        ESP_LOGW(TAG, "inflight latch failed; event may be re-published on retry");
    }

    int64_t mid = e.measure_id;
    measurement_event_free(&e);
    return make_result(ESP_OK, "published event id=%lld msg_id=%d", (long long)mid, msg_id);
}

cmd_result_t cmd_uart_stream_query(uint8_t channel, const char *cmd,
                                   const char *sentinel, uint32_t timeout_ms,
                                   char *out, size_t out_cap, size_t *out_len)
{
    if (!s_initialized || s_cfg.uart_stream_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART stream query not available");
    }
    if (cmd == NULL || out == NULL || out_len == NULL || out_cap < 2) {
        return make_result(ESP_ERR_INVALID_ARG, "stream_query: bad args");
    }
    device_commands_measurement_begin();
    esp_err_t err = s_cfg.uart_stream_query(channel, cmd, "\n", sentinel,
                                            out, out_cap, out_len, timeout_ms);
    device_commands_measurement_end();
    if (err == ESP_ERR_TIMEOUT) {
        return make_result(ESP_ERR_TIMEOUT, "stream timeout (no '%s')", sentinel ? sentinel : "");
    }
    if (err != ESP_OK) {
        return make_result(err, "stream_query ch%u failed: %s", channel, esp_err_to_name(err));
    }
    return make_result(ESP_OK, "stream ch%u: %u bytes", channel, (unsigned)*out_len);
}

void device_commands_on_mqtt_disconnect(void)
{
    if (s_inflight_mtx == NULL) return;
    if (xSemaphoreTake(s_inflight_mtx, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    int64_t mid = s_inflight_measure_id;
    bool had = (mid >= 0 && s_inflight_msg_id >= 0);
    s_inflight_measure_id = -1;
    s_inflight_msg_id     = -1;
    xSemaphoreGive(s_inflight_mtx);

    if (had && s_cfg.mark_event_pending != NULL) {
        s_cfg.mark_event_pending(mid);
    }
}

/* ── Measurement-activity gate ──────────────────────────────────────────
 * The sync runner consults device_commands_measurement_active() and pauses
 * publishing while a UART measurement is in progress, draining during idle. */
void device_commands_measurement_begin(void) { s_measurement_active++; }
void device_commands_measurement_end(void)
{
    if (s_measurement_active > 0) s_measurement_active--;
}
bool device_commands_measurement_active(void) { return s_measurement_active > 0; }

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
    device_commands_measurement_begin();
    esp_err_t err = s_cfg.uart_query(channel, cmd, extra, extra_len,
                                     expect_raw, response, timeout_ms);
    device_commands_measurement_end();
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
    device_commands_measurement_begin();
    esp_err_t err = s_cfg.uart_ping(channel, connected);
    device_commands_measurement_end();
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

/* Generic ASCII line-oriented query — sends "<cmd><terminator>", reads one
 * line back, discards the first line if it echoes the command verbatim, and
 * optionally stores the response in the measurements DB. See
 * cmd_uart_text_query() in device_commands.h for the contract. */
cmd_result_t cmd_uart_text_query(uint8_t channel,
                                 const char *cmd, const char *terminator,
                                 uint32_t timeout_ms, bool save,
                                 char *out_resp, size_t resp_cap,
                                 size_t *resp_len)
{
    if (!s_initialized || s_cfg.uart_text_query == NULL) {
        return make_result(ESP_ERR_NOT_SUPPORTED, "UART sensors not available");
    }
    if (channel >= UART_SENSOR_NUM_CHANNELS) {
        return make_result(ESP_ERR_INVALID_ARG, "invalid channel %u", channel);
    }
    if (cmd == NULL || terminator == NULL || out_resp == NULL ||
        resp_len == NULL || resp_cap < 2) {
        return make_result(ESP_ERR_INVALID_ARG, "bad args");
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t start_ms_val = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    int64_t t0_us = esp_timer_get_time();  /* monotonic, for echo-retry budget */
    *resp_len = 0;
    out_resp[0] = '\0';

    /* First attempt — full timeout budget. */
    device_commands_measurement_begin();
    esp_err_t err = s_cfg.uart_text_query(channel, cmd, terminator,
                                          out_resp, resp_cap, resp_len, timeout_ms);

    /* Echo handling: if the first line equals the sent cmd, throw it away and
     * read the next line with whatever budget is left. The line we just read
     * is already in out_resp without the terminator. */
    if (err == ESP_OK) {
        size_t cmd_len = strlen(cmd);
        if (*resp_len == cmd_len && memcmp(out_resp, cmd, cmd_len) == 0) {
            int64_t elapsed_us = esp_timer_get_time() - t0_us;
            int64_t remaining_ms = ((int64_t)timeout_ms * 1000 - elapsed_us) / 1000;
            if (remaining_ms <= 0) {
                err = ESP_ERR_TIMEOUT;
                *resp_len = 0;
                out_resp[0] = '\0';
            } else {
                /* Read another line — pass an empty cmd so nothing is re-sent. */
                *resp_len = 0;
                out_resp[0] = '\0';
                err = s_cfg.uart_text_query(channel, "", terminator,
                                            out_resp, resp_cap, resp_len,
                                            (uint32_t)remaining_ms);
            }
        }
    }
    device_commands_measurement_end();

    /* On hard error (other than timeout) propagate without saving. */
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return make_result(err, "uart_text_query ch%u failed: %s",
                           channel, esp_err_to_name(err));
    }

    /* save=true: write one row tagged sensor=uart_chN, quantity=response.
     * On timeout the row carries value_text="" (empty string) to indicate
     * "queried but nothing came back". */
    if (save && s_cfg.store_event != NULL && s_cfg.next_id != NULL) {
        int64_t mid = 0;
        if (s_cfg.next_id(&mid) == ESP_OK) {
            gettimeofday(&tv, NULL);
            int64_t end_ms_val = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
            char sensor[16];
            snprintf(sensor, sizeof(sensor), "uart_ch%u", channel);
            /* One event, payload {"response":"..."}; cJSON escapes the string.
             * On timeout out_resp is "" (queried but nothing came back). */
            cJSON *p = cJSON_CreateObject();
            if (p != NULL) {
                cJSON_AddStringToObject(p, "response", out_resp);
                char *pj = cJSON_PrintUnformatted(p);
                cJSON_Delete(p);
                if (pj != NULL) {
                    esp_err_t store_err = s_cfg.store_event(mid, "", sensor,
                                                            start_ms_val, end_ms_val, NULL, pj);
                    free(pj);
                    if (store_err != ESP_OK) {
                        ESP_LOGW(TAG, "uart_text_query: save failed: %s", esp_err_to_name(store_err));
                    }
                }
            }
        }
    }

    if (err == ESP_ERR_TIMEOUT) {
        return make_result(ESP_ERR_TIMEOUT, "ch%u: no response within %ums",
                           channel, (unsigned)timeout_ms);
    }
    return make_result(ESP_OK, "ch%u: %u bytes", channel, (unsigned)*resp_len);
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

