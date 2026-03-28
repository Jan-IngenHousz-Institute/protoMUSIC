#ifndef AMBYTE_PERSISTENCE_PORT_H
#define AMBYTE_PERSISTENCE_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t sensor_id;
    int64_t measure_id;
    char measure_type[32];
    time_t timestamp;
    char data_type[32];
    float value;
    bool synced;
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

#ifdef __cplusplus
}
#endif

#endif
