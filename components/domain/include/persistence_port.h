#ifndef AMBYTE_PERSISTENCE_PORT_H
#define AMBYTE_PERSISTENCE_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEASUREMENT_SYNC_PENDING  = 0,
    MEASUREMENT_SYNC_INFLIGHT = 1,
    MEASUREMENT_SYNC_SYNCED   = 2,
} measurement_sync_state_t;

typedef struct {
    int64_t sensor_id;
    int64_t measure_id;
    char measure_type[32];
    time_t timestamp;
    char data_type[32];
    float value;
    measurement_sync_state_t sync_state;
} measurement_record_t;

typedef esp_err_t (*measurement_store_fn)(const measurement_record_t *records, size_t count);
typedef esp_err_t (*measurement_query_fn)(const char *measure_type, time_t from, time_t to,
                                          measurement_record_t *out, size_t max, size_t *count);
typedef esp_err_t (*measurement_count_fn)(const char *measure_type, size_t *count);
typedef esp_err_t (*measurement_next_id_fn)(int64_t *out_id);
typedef esp_err_t (*measurement_query_unsynced_fn)(const char *measure_type,
                                                    measurement_record_t *out, size_t max,
                                                    size_t *count);
typedef esp_err_t (*measurement_mark_synced_fn)(int64_t measure_id);
typedef esp_err_t (*measurement_mark_inflight_fn)(int64_t measure_id);
typedef esp_err_t (*measurement_mark_pending_fn)(int64_t measure_id);

/* Returns all rows for one logical measurement group, ordered by dataType */
typedef esp_err_t (*measurement_query_by_id_fn)(int64_t measure_id,
                                                 measurement_record_t *out, size_t max,
                                                 size_t *count);

/* Atomically claims the oldest PENDING group for measure_type (PENDING→INFLIGHT) and
 * returns its measureID. Returns ESP_ERR_NOT_FOUND when nothing is pending. */
typedef esp_err_t (*measurement_claim_next_pending_fn)(const char *measure_type,
                                                        int64_t *out_measure_id);

#ifdef __cplusplus
}
#endif

#endif
