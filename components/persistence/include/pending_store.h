#ifndef AMBYTE_PENDING_STORE_H
#define AMBYTE_PENDING_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "persistence_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Magic byte tagging each on-disk entry. Bump this whenever the on-disk
 * layout of measurement_record_t changes — pending_store_init detects the
 * mismatch, renames the legacy ring file to *.legacy_<unix_ts>, and starts
 * fresh. Old entries are preserved on disk for forensic recovery. */
#define PENDING_ENTRY_MAGIC 0xA5B72001U

typedef struct {
    uint32_t magic;
    measurement_record_t record;
    uint32_t crc32;
} pending_entry_t;

esp_err_t pending_store_init(void);
esp_err_t pending_store_append(const measurement_record_t *records, size_t count);
esp_err_t pending_store_read(pending_entry_t *out, size_t max, size_t *count);
esp_err_t pending_store_remove(size_t count);
size_t    pending_store_count(void);
esp_err_t pending_store_next_id(int64_t *out_id);
void      pending_store_seed_max_id(int64_t sqlite_max_id);

#ifdef __cplusplus
}
#endif

#endif
