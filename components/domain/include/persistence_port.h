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

typedef enum {
    MEASUREMENT_SYNC_PENDING  = 0,
    MEASUREMENT_SYNC_INFLIGHT = 1,
    MEASUREMENT_SYNC_SYNCED   = 2,
} measurement_sync_state_t;

/* One row per measured quantity. Rows that come from the same logical
 * sampling event share `measure_id` (PK is (measure_id, quantity)).
 *
 * device:    type of external module producing the row, or "" when the sensor
 *            is on the ambyte board itself (translated to SQL NULL).
 * sensor_id: I2C address etc.; -1 sentinel means "no id" (translated to NULL).
 * metadata:  JSON object as a string; "" means "no metadata" (translated to NULL).
 * value_is_string: discriminator selecting value_real vs value_text. */
typedef struct {
    int64_t  measure_id;
    char     quantity[32];          /* "temperature", "humidity", ... */
    int64_t  start_ticks_ms;        /* UTC ms since epoch */
    int64_t  end_ticks_ms;          /* UTC ms since epoch */
    char     device[24];            /* "" = onboard */
    char     sensor[24];            /* "BME280", "AS7343", ... */
    int64_t  sensor_id;             /* -1 = NULL */
    char     metadata[192];         /* JSON string, "" = NULL */
    bool     value_is_string;
    float    value_real;            /* valid when !value_is_string */
    char     value_text[64];        /* valid when value_is_string */
    measurement_sync_state_t sync_state;
} measurement_record_t;

/* Sentinel used in records to indicate "no sensor_id" before the SQLite layer
 * translates it to SQL NULL. */
#define MEASUREMENT_SENSOR_ID_NONE  ((int64_t)-1)

/* ── port function-pointer typedefs ──────────────────────────────────── */

/* Append records into the pending FIFO (PENDING on arrival). */
typedef esp_err_t (*measurement_store_fn)(const measurement_record_t *records, size_t count);

/* Range query by quantity name. */
typedef esp_err_t (*measurement_query_fn)(const char *quantity,
                                          int64_t from_ms, int64_t to_ms,
                                          measurement_record_t *out, size_t max,
                                          size_t *count);

/* Count distinct measure_ids that have at least one row of the given quantity. */
typedef esp_err_t (*measurement_count_fn)(const char *quantity, size_t *count);

/* Allocate the next monotonic measure_id. */
typedef esp_err_t (*measurement_next_id_fn)(int64_t *out_id);

/* All PENDING rows of the given quantity, up to `max`. */
typedef esp_err_t (*measurement_query_unsynced_fn)(const char *quantity,
                                                    measurement_record_t *out, size_t max,
                                                    size_t *count);

/* All rows of one measurement group, ordered by quantity. */
typedef esp_err_t (*measurement_query_by_id_fn)(int64_t measure_id,
                                                 measurement_record_t *out, size_t max,
                                                 size_t *count);

/* ── batch claim / mark (used by the sync runner) ────────────────────── *
 *
 * The sync layer pulls a batch of PENDING rows in one shot. Groups are kept
 * whole — `claim_next_pending_batch` only includes a measure_id if every one
 * of its PENDING rows fits inside `max_rows`. The returned rows are tagged
 * INFLIGHT atomically in the same transaction.
 *
 * After publishing succeeds, `mark_batch_synced` flips every row of every
 * listed measure_id from INFLIGHT to SYNCED. On failure, `mark_batch_pending`
 * rolls them back to PENDING so the next cycle retries. */
typedef esp_err_t (*measurement_claim_next_pending_batch_fn)(measurement_record_t *out,
                                                              size_t max_rows,
                                                              size_t *out_count);

typedef esp_err_t (*measurement_mark_batch_synced_fn)(const int64_t *measure_ids, size_t count);

typedef esp_err_t (*measurement_mark_batch_pending_fn)(const int64_t *measure_ids, size_t count);

#ifdef __cplusplus
}
#endif

#endif
