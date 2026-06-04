#ifndef AMBYTE_DEVICE_COMMANDS_H
#define AMBYTE_DEVICE_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "device_status_port.h"
#include "messaging_port.h"
#include "persistence_port.h"
#include "sensing_port.h"
#include "uart_sensor_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t status;
    char message[256];
} cmd_result_t;

typedef struct {
    /* Sensing ports */
    sensor_read_fn              read_env;
    clock_read_fn               read_clock;

    /* Persistence ports — one row per measurement event (event-document model) */
    measurement_next_id_fn            next_id;
    measurement_store_event_fn        store_event;
    measurement_claim_next_event_fn   claim_next_event;   /* used by sync_runner */
    measurement_mark_event_synced_fn  mark_event_synced;
    measurement_mark_event_pending_fn mark_event_pending;

    /* Status port */
    status_set_fn               set_status;

    /* SD-card readiness probe (used by Lua main.lua to gate measurement
     * rounds when the card is out). NULL = SD layer absent. */
    bool                       (*sd_ready)(void);

    /* Messaging ports (Phase 6A) */
    message_publish_fn                  publish;
    message_is_connected_fn             message_is_connected;
    message_set_publish_ack_handler_fn  set_publish_ack_handler;

    /* Topic config (Phase 6A) */
    const char                         *topic_root;
    const char                         *device_id;

    /* Payload metadata (provisioned via BLE) */
    const char                         *protocol_id;
    const char                         *device_name;
    const char                         *device_version;
    const char                         *device_firmware;
    const char                         *firmware_version;

    /* UART sensor ports (Phase 7) */
    uart_sensor_query_fn                uart_query;        /* AMBIT binary */
    uart_sensor_ping_fn                 uart_ping;
    uart_sensor_status_fn               uart_status;
    uart_sensor_text_query_fn           uart_text_query;   /* generic ASCII line */
    uart_sensor_stream_query_fn         uart_stream_query; /* multi-line until sentinel */
} device_commands_config_t;

esp_err_t device_commands_init(const device_commands_config_t *cfg);

/* Measurement-activity gate. Bracket any latency-sensitive sensor transaction
 * with begin()/end(); the background sync runner pauses publishing while the
 * count is non-zero and drains once it returns to idle. Safe to nest. */
void device_commands_measurement_begin(void);
void device_commands_measurement_end(void);
bool device_commands_measurement_active(void);

cmd_result_t cmd_set_rgb(uint8_t r, uint8_t g, uint8_t b);
cmd_result_t cmd_read_rtc(time_t *out_time);
cmd_result_t cmd_device_status(bool *bme_ready, bool *rtc_ready, time_t *rtc_time);
cmd_result_t cmd_read_env(float *temp, float *hum, float *pres);

/* Drive a PWM signal on GPIO4 via LEDC. duty_pct is 0..100 (float precision);
 * freq_hz is the PWM frequency in Hz (e.g. 10000). When enable is false the
 * output is stopped and the pin held low (duty/freq ignored). The duty
 * resolution is chosen automatically from freq_hz (up to 14-bit). */
cmd_result_t cmd_pwm(float duty_pct, uint32_t freq_hz, bool enable);

/* Read BME280 and persist temperature/humidity/pressure as one event row
 * (payload {"temperature":..,"humidity":..,"pressure":..}). The background sync
 * task (sync_runner) publishes it as one MQTT message. Pass NULL to ignore the
 * allocated measure_id. */
cmd_result_t cmd_record_env(int64_t *out_measure_id);

/* Returns true when the SD card is mounted (and therefore the SQLite event DB
 * is writable). main.lua should consult this before starting a measurement
 * round so it doesn't measure into a closed DB. */
cmd_result_t cmd_sd_ready(bool *out_ready);
cmd_result_t cmd_log(const char *msg);
cmd_result_t cmd_sleep_ms(uint32_t ms);

/* Store one measurement event. payload_json is a JSON object of quantities
 * (required); metadata_json may be NULL; device "" / NULL = onboard. Writes
 * straight to SQLite — requires the SD-backed DB. */
cmd_result_t cmd_store_event(int64_t measure_id, const char *device, const char *sensor,
                             int64_t start_ms, int64_t end_ms,
                             const char *metadata_json, const char *payload_json);

/* Send an ASCII command and read multiple response lines until one contains
 * `sentinel` (or timeout). Pre-wakes the port. Used for the AMBIT PLOTTING run.
 * `out` is NUL-terminated; *out_len excludes the NUL. */
cmd_result_t cmd_uart_stream_query(uint8_t channel, const char *cmd,
                                   const char *sentinel, uint32_t timeout_ms,
                                   char *out, size_t out_cap, size_t *out_len);
cmd_result_t cmd_next_measure_id(int64_t *out_id);

/* MQTT commands (Phase 6A) */
/* Publish the next pending event as one MQTT message (one measure_id = one
 * message; used by the sync_runner). */
cmd_result_t cmd_mqtt_publish_next_event(void);
cmd_result_t cmd_mqtt_status(void);

/* UART sensor commands — raw (Phase 7) */
cmd_result_t cmd_uart_query(uint8_t channel, const uint8_t cmd[8],
                            const uint8_t *extra, size_t extra_len,
                            size_t expect_raw,
                            uart_sensor_response_t *response,
                            uint32_t timeout_ms);
cmd_result_t cmd_uart_ping(uint8_t channel, bool *connected);
cmd_result_t cmd_uart_status(void);

/* Generic ASCII line-oriented UART query.
 *
 * Sends `cmd` followed by `terminator` over UART channel `channel`, then
 * reads one line (until `terminator` again) into `out_resp` or aborts after
 * `timeout_ms`. If the first line echoes the sent command verbatim it is
 * discarded and the next line is returned.
 *
 * When `save` is true, the response (or an empty string on timeout) is
 * stored as a single measurement event tagged
 *   sensor  = "uart_ch<N>"
 *   device  = NULL (onboard)
 *   payload = {"response":"<text>"}
 * via cmd_store_event. `out_resp` is always set (NUL-terminated, possibly
 * empty) on return.
 */
cmd_result_t cmd_uart_text_query(uint8_t channel,
                                 const char *cmd, const char *terminator,
                                 uint32_t timeout_ms, bool save,
                                 char *out_resp, size_t resp_cap,
                                 size_t *resp_len);

/* Ambit sensor commands — typed wrappers (Phase 7) */

/* Configuration (ack-only — no CMD_END) */
cmd_result_t cmd_ambit_set_gains(uint8_t ch, uint8_t fluo, uint8_t fluoref,
                                  uint8_t ir, uint8_t irref,
                                  uint8_t sun, uint8_t leaf);
cmd_result_t cmd_ambit_set_currents(uint8_t ch, uint8_t i620, uint8_t i720,
                                     uint8_t ir);
cmd_result_t cmd_ambit_config_detector(uint8_t ch);

/* Queries (immediate raw response) */
cmd_result_t cmd_ambit_get_temp(uint8_t ch, float *leaf_temp, float *chip_temp);
cmd_result_t cmd_ambit_get_spec(uint8_t ch, uint16_t spec[10], float *par);
cmd_result_t cmd_ambit_get_temp_raw(uint8_t ch, float *leaf, float *leaf1,
                                     float *chip, int16_t raw[4]);
cmd_result_t cmd_ambit_get_info(uint8_t ch, uint8_t info_type,
                                 uint8_t *out, size_t out_size, size_t *out_len);

/* Measurements (FSM response) */
cmd_result_t cmd_ambit_run(uint8_t ch, const uint8_t *run_arr, uint8_t arr_len,
                            uint8_t led_persist, bool allow_interrupt,
                            uart_sensor_response_t *response, uint32_t timeout_ms);
cmd_result_t cmd_ambit_run_mpf(uint8_t ch, uint16_t length, uint8_t interval,
                                bool change_act, uint8_t act,
                                uart_sensor_response_t *response, uint32_t timeout_ms);

/* Actions (wait for CMD_END, no response data) */
cmd_result_t cmd_ambit_blink(uint8_t ch, uint8_t ambit_id, uint8_t intensity);
cmd_result_t cmd_ambit_calibrate_baseline(uint8_t ch);
cmd_result_t cmd_ambit_actinic(uint8_t ch, uint8_t type, uint8_t var, uint8_t var2);

/* Write commands (extra data buffered, wait for CMD_END) */
cmd_result_t cmd_ambit_set_metadata(uint8_t ch, const uint8_t *metadata, size_t len);

/* Call from the Wi-Fi disconnect event handler to clear any in-flight publish slot */
void device_commands_on_mqtt_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif
