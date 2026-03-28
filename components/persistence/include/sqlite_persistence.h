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
 *   3. Seed measureID counter from max(SQLite, pending)
 *   4. Drain any pending LittleFS records into SQLite
 *   5. Start background flush task
 *
 * SD card and LittleFS must be mounted before calling this.
 */
esp_err_t sqlite_persistence_init(void);

/* Port implementations — pass these function pointers to device_commands */
esp_err_t sqlite_persistence_store(const measurement_record_t *records, size_t count);
esp_err_t sqlite_persistence_query(const char *measure_type, time_t from, time_t to,
                                   measurement_record_t *out, size_t max, size_t *count);
esp_err_t sqlite_persistence_count(const char *measure_type, size_t *count);
esp_err_t sqlite_persistence_next_id(int64_t *out_id);
esp_err_t sqlite_persistence_query_unsynced(const char *measure_type,
                                            measurement_record_t *out, size_t max,
                                            size_t *count);
esp_err_t sqlite_persistence_mark_synced(int64_t measure_id);

/* Getters for function pointers */
measurement_store_fn          sqlite_persistence_get_store_fn(void);
measurement_query_fn          sqlite_persistence_get_query_fn(void);
measurement_count_fn          sqlite_persistence_get_count_fn(void);
measurement_next_id_fn        sqlite_persistence_get_next_id_fn(void);
measurement_query_unsynced_fn sqlite_persistence_get_query_unsynced_fn(void);
measurement_mark_synced_fn    sqlite_persistence_get_mark_synced_fn(void);

#ifdef __cplusplus
}
#endif

#endif
