#ifndef AMBYTE_PERSISTENCE_PORT_H
#define AMBYTE_PERSISTENCE_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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

/* One row per measurement *event*. All quantities measured together (a BME280
 * read's T/H/P, or an AMBIT run's leaf_temp/fluor/fluoRef arrays) live in one
 * row, serialised as a JSON object in `payload_json`. This collapses the old
 * row-per-quantity design into a single store-and-forward document.
 *
 * device:        external module type ("ambit"), or "" when onboard (SQL NULL).
 * metadata_json: JSON object string, or NULL.
 * payload_json:  JSON object of quantities, e.g. {"temperature":23.1,...} or
 *                {"fluor":[...],"fluoRef":[...]}. Heap on claim; caller frees
 *                via measurement_event_free.
 */
typedef struct {
    int64_t  measure_id;
    char     device[24];
    char     sensor[24];
    int64_t  start_ticks_ms;
    int64_t  end_ticks_ms;
    char    *metadata_json;
    char    *payload_json;
    measurement_sync_state_t sync_state;
} measurement_event_t;

static inline void measurement_event_free(measurement_event_t *e)
{
    if (e == NULL) return;
    free(e->metadata_json);
    free(e->payload_json);
    e->metadata_json = NULL;
    e->payload_json  = NULL;
}

/* ── port function-pointer typedefs ──────────────────────────────────── */

/* Allocate the next monotonic measure_id. */
typedef esp_err_t (*measurement_next_id_fn)(int64_t *out_id);

/* Store one event. payload_json is required (a JSON object of quantities);
 * metadata_json may be NULL. device "" / NULL = onboard. Writes straight to
 * SQLite — requires the DB to be available. */
typedef esp_err_t (*measurement_store_event_fn)(int64_t measure_id,
                                                const char *device, const char *sensor,
                                                int64_t start_ms, int64_t end_ms,
                                                const char *metadata_json,
                                                const char *payload_json);

/* Claim the oldest PENDING event, flip it INFLIGHT, return it (heap strings —
 * free with measurement_event_free). ESP_ERR_NOT_FOUND when none pending. */
typedef esp_err_t (*measurement_claim_next_event_fn)(measurement_event_t *out);

typedef esp_err_t (*measurement_mark_event_synced_fn)(int64_t measure_id);
typedef esp_err_t (*measurement_mark_event_pending_fn)(int64_t measure_id);

#ifdef __cplusplus
}
#endif

#endif
