#ifndef AMBYTE_SQLITE_PERSISTENCE_H
#define AMBYTE_SQLITE_PERSISTENCE_H

#include "esp_err.h"
#include "persistence_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the persistence layer:
 *   1. Init pending store (LittleFS)
 *   2. Open SQLite DB on SD card (if available)
 *   3. Run schema migration (legacy `measurements` → archive + fresh measurements_v2)
 *   4. Reset stale INFLIGHT rows to PENDING (crash-recovery)
 *   5. Seed measure_id counter from MAX(measure_id)
 *   6. Start background flush task
 *
 * SD card and LittleFS must be mounted before calling this.
 */
esp_err_t sqlite_persistence_init(void);

/* Port implementations — pass these function pointers to device_commands */
esp_err_t sqlite_persistence_store(const measurement_record_t *records, size_t count);
esp_err_t sqlite_persistence_query(const char *quantity, int64_t from_ms, int64_t to_ms,
                                    measurement_record_t *out, size_t max, size_t *count);
esp_err_t sqlite_persistence_count(const char *quantity, size_t *count);
esp_err_t sqlite_persistence_next_id(int64_t *out_id);
esp_err_t sqlite_persistence_query_unsynced(const char *quantity,
                                             measurement_record_t *out, size_t max,
                                             size_t *count);
esp_err_t sqlite_persistence_query_by_id(int64_t measure_id,
                                          measurement_record_t *out, size_t max,
                                          size_t *count);

/* Batch claim / mark used by the sync runner.
 * claim_next_pending_batch returns up to `max_rows` rows (whole groups only)
 * and atomically marks them INFLIGHT. mark_batch_synced / mark_batch_pending
 * are the per-publish post-PUBACK handlers.
 */
esp_err_t sqlite_persistence_claim_next_pending_batch(measurement_record_t *out,
                                                       size_t max_rows,
                                                       size_t *out_count);
esp_err_t sqlite_persistence_mark_batch_synced(const int64_t *measure_ids, size_t count);
esp_err_t sqlite_persistence_mark_batch_pending(const int64_t *measure_ids, size_t count);

/* Getters for function pointers */
measurement_store_fn                    sqlite_persistence_get_store_fn(void);
measurement_query_fn                    sqlite_persistence_get_query_fn(void);
measurement_count_fn                    sqlite_persistence_get_count_fn(void);
measurement_next_id_fn                  sqlite_persistence_get_next_id_fn(void);
measurement_query_unsynced_fn           sqlite_persistence_get_query_unsynced_fn(void);
measurement_query_by_id_fn              sqlite_persistence_get_query_by_id_fn(void);
measurement_claim_next_pending_batch_fn sqlite_persistence_get_claim_next_pending_batch_fn(void);
measurement_mark_batch_synced_fn        sqlite_persistence_get_mark_batch_synced_fn(void);
measurement_mark_batch_pending_fn       sqlite_persistence_get_mark_batch_pending_fn(void);

#ifdef __cplusplus
}
#endif

#endif
