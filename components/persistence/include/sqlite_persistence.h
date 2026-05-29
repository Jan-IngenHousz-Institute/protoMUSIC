#ifndef AMBYTE_SQLITE_PERSISTENCE_H
#define AMBYTE_SQLITE_PERSISTENCE_H

#include "esp_err.h"
#include "persistence_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise the persistence layer:
 *   1. Open SQLite on the SD card (if mounted)
 *   2. Migrate to the `events` schema (archive any pre-v3 DB, clean slate)
 *   3. Reset stale INFLIGHT rows to PENDING (crash recovery)
 *   4. Seed the measure_id counter from MAX(measure_id)
 *
 * One row per measurement event; all quantities are a JSON object in `payload`.
 */
esp_err_t sqlite_persistence_init(void);

/* Called by the SD hot-plug monitor (Phase 2) when the card is pulled/inserted. */
esp_err_t sqlite_persistence_on_sd_lost(void);
esp_err_t sqlite_persistence_on_sd_restored(void);

/* Event store / claim / mark. */
esp_err_t sqlite_persistence_next_id(int64_t *out_id);
esp_err_t sqlite_persistence_store_event(int64_t measure_id,
                                         const char *device, const char *sensor,
                                         int64_t start_ms, int64_t end_ms,
                                         const char *metadata_json,
                                         const char *payload_json);
esp_err_t sqlite_persistence_claim_next_event(measurement_event_t *out);
esp_err_t sqlite_persistence_mark_event_synced(int64_t measure_id);
esp_err_t sqlite_persistence_mark_event_pending(int64_t measure_id);

/* Getters for function pointers */
measurement_next_id_fn            sqlite_persistence_get_next_id_fn(void);
measurement_store_event_fn        sqlite_persistence_get_store_event_fn(void);
measurement_claim_next_event_fn   sqlite_persistence_get_claim_next_event_fn(void);
measurement_mark_event_synced_fn  sqlite_persistence_get_mark_event_synced_fn(void);
measurement_mark_event_pending_fn sqlite_persistence_get_mark_event_pending_fn(void);

#ifdef __cplusplus
}
#endif

#endif
