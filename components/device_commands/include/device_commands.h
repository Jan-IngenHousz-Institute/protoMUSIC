#ifndef AMBYTE_DEVICE_COMMANDS_H
#define AMBYTE_DEVICE_COMMANDS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "device_status_port.h"
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
    sensor_read_fn              read_env;
    clock_read_fn               read_clock;
    measurement_store_fn        store;
    measurement_query_fn        query;
    measurement_count_fn        count;
    measurement_next_id_fn      next_id;
    measurement_query_unsynced_fn query_unsynced;
    measurement_mark_synced_fn  mark_synced;
    status_set_fn               set_status;
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
cmd_result_t cmd_mark_synced(int64_t measure_id);

#ifdef __cplusplus
}
#endif

#endif
