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
cmd_result_t cmd_mqtt_publish_measurement(int64_t measure_id);
cmd_result_t cmd_mqtt_publish_unsynced(const char *measure_type);
cmd_result_t cmd_mqtt_status(void);

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
