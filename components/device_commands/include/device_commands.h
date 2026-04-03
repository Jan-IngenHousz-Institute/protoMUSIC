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

typedef bool (*certs_status_fn)(void);

typedef struct {
    /* Sensing ports */
    sensor_read_fn              read_env;
    clock_read_fn               read_clock;

    /* Persistence ports */
    measurement_store_fn              store;
    measurement_query_fn              query;
    measurement_count_fn              count;
    measurement_next_id_fn            next_id;
    measurement_query_unsynced_fn     query_unsynced;
    measurement_mark_synced_fn        mark_synced;
    measurement_mark_inflight_fn      mark_inflight;
    measurement_mark_pending_fn       mark_pending;
    measurement_query_by_id_fn        query_by_id;
    measurement_claim_next_pending_fn claim_next_pending;

    /* Status port */
    status_set_fn               set_status;

    /* Messaging ports (Phase 6A) */
    message_publish_fn                  publish;
    message_is_connected_fn             message_is_connected;
    message_set_publish_ack_handler_fn  set_publish_ack_handler;

    /* Inbound subscribe port (Phase 6B) */
    message_subscribe_fn                subscribe;

    /* Topic config (Phase 6A) */
    const char                         *topic_root;
    const char                         *device_id;

    /* Cert status (Phase 6C) */
    certs_status_fn                     certs_status;

    /* Payload metadata (provisioned via BLE) */
    const char                         *protocol_id;
    const char                         *device_name;
    const char                         *device_version;
    const char                         *device_firmware;
    const char                         *firmware_version;

    /* UART sensor ports (Phase 7) */
    uart_sensor_query_fn                uart_query;
    uart_sensor_ping_fn                 uart_ping;
    uart_sensor_status_fn               uart_status;
} device_commands_config_t;

esp_err_t device_commands_init(const device_commands_config_t *cfg);

cmd_result_t cmd_set_rgb(uint8_t r, uint8_t g, uint8_t b);
cmd_result_t cmd_read_rtc(time_t *out_time);
cmd_result_t cmd_device_status(bool *bme_ready, bool *rtc_ready, time_t *rtc_time);
cmd_result_t cmd_read_env(float *temp, float *hum, float *pres);
cmd_result_t cmd_log(const char *msg);
cmd_result_t cmd_sleep_ms(uint32_t ms);
cmd_result_t cmd_store_measurement(const measurement_record_t *records, size_t count);
cmd_result_t cmd_query_measurements(const char *measure_type, time_t from, time_t to,
                                    measurement_record_t *out, size_t max, size_t *count);
cmd_result_t cmd_measurement_count(const char *measure_type, size_t *count);
cmd_result_t cmd_next_measure_id(int64_t *out_id);
cmd_result_t cmd_query_unsynced(const char *measure_type, measurement_record_t *out,
                                size_t max, size_t *count);

/* MQTT commands (Phase 6A) */
cmd_result_t cmd_mqtt_publish(const char *topic, const char *payload);
cmd_result_t cmd_mqtt_publish_raw(const char *payload);
cmd_result_t cmd_mqtt_publish_measurement(int64_t measure_id);
cmd_result_t cmd_mqtt_publish_unsynced(const char *measure_type);
cmd_result_t cmd_mqtt_status(void);

/* Cert status (Phase 6C) */
cmd_result_t cmd_cert_status(void);

/* UART sensor commands — raw (Phase 7) */
cmd_result_t cmd_uart_query(uint8_t channel, const uint8_t cmd[8],
                            const uint8_t *extra, size_t extra_len,
                            size_t expect_raw,
                            uart_sensor_response_t *response,
                            uint32_t timeout_ms);
cmd_result_t cmd_uart_ping(uint8_t channel, bool *connected);
cmd_result_t cmd_uart_status(void);

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

/* Parse a JSON command object and dispatch to the appropriate cmd_* function.
 * Expected wire format: {"cmd": "<name>", ...params...}
 * Returns the result of the dispatched command, or an error result on parse failure. */
cmd_result_t cmd_dispatch_json(const char *json, size_t len);

/* Subscribe to the inbound command topic (<root>/<device_id>/cmd) using the
 * registered subscribe port. Safe to call before MQTT connects; the mqtt_client
 * subscription table re-subscribes automatically on each reconnect. */
void device_commands_subscribe_inbound(void);

#ifdef __cplusplus
}
#endif

#endif
