#include "sqlite_persistence.h"
#include "pending_store.h"
#include "sd_card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sqlite3.h"

#define TAG "sqlite_persist"
#define DB_PATH "/sdcard/measurements.db"
#define SCHEMA_VERSION 2
#define FLUSH_INTERVAL_MS 5000
#define FLUSH_BATCH_SIZE  16
/* 8 KiB. The entries buffer (FLUSH_BATCH_SIZE × pending_entry_t) used to live
 * on this stack and was ~1.8 KiB with the old struct; with the new ~380 B
 * record it would be ~6 KiB and overflow even before SQLite is touched. The
 * buffer is now heap-allocated, but bumping the stack gives headroom for
 * SQLite's own deep call chain and any cJSON/format helpers down the line. */
#define FLUSH_TASK_STACK  8192
#define FLUSH_ERROR_CHECK_THRESHOLD    3   /* run quick_check after N consecutive flush failures */
#define FLUSH_ERROR_DISABLE_THRESHOLD 10   /* disable DB after N consecutive flush failures */
#define INTEGRITY_CHECK_INTERVAL     100   /* run quick_check every N successful flush cycles (~8 min) */

static sqlite3 *s_db = NULL;
static SemaphoreHandle_t s_sqlite_mutex = NULL;
static StaticSemaphore_t s_sqlite_mutex_storage;
static bool s_db_available = false;
static TaskHandle_t s_flush_task = NULL;
static int s_flush_errors = 0;
static int s_flush_cycles = 0;

static esp_err_t db_open_and_configure(void);  /* forward decl for recovery */

/* ── helpers ─────────────────────────────────────────────────────────── */

static void increment_nvs_counter(const char *key)
{
    nvs_handle_t h;
    if (nvs_open("diag", NVS_READWRITE, &h) == ESP_OK) {
        uint32_t n = 0;
        nvs_get_u32(h, key, &n);
        nvs_set_u32(h, key, n + 1);
        nvs_commit(h);
        nvs_close(h);
    }
}

static esp_err_t exec_pragma(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "PRAGMA failed: %s — %s", sql, err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int integrity_callback(void *arg, int ncols, char **values, char **names)
{
    (void)ncols;
    (void)names;
    bool *ok = (bool *)arg;
    if (ncols > 0 && values[0] && strcmp(values[0], "ok") != 0) {
        *ok = false;
    }
    return 0;
}

/* Read PRAGMA user_version; returns 0 on a fresh DB. */
static int read_user_version(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

static bool table_exists(sqlite3 *db, const char *name)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

/* Run PRAGMA quick_check inside an already-held mutex.
 * If corrupt: close DB, rename to .corrupt, reopen fresh.
 * Returns true if DB is healthy, false if recovery was attempted. */
static bool run_quick_check_locked(void)
{
    if (s_db == NULL) {
        return false;
    }
    bool ok = true;
    char *err_msg = NULL;
    int rc = sqlite3_exec(s_db, "PRAGMA quick_check;", integrity_callback,
                          &ok, &err_msg);
    sqlite3_free(err_msg);

    if (ok && rc == SQLITE_OK) {
        return true;
    }

    ESP_LOGE(TAG, "quick_check failed — recovering DB");
    sqlite3_close(s_db);
    s_db = NULL;

    rename(DB_PATH, DB_PATH ".corrupt");
    rename(DB_PATH "-wal", DB_PATH "-wal.corrupt");
    rename(DB_PATH "-shm", DB_PATH "-shm.corrupt");
    rename(DB_PATH "-journal", DB_PATH "-journal.corrupt");
    increment_nvs_counter("db_corrupt_cnt");

    esp_err_t err = db_open_and_configure();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DB re-open after recovery failed");
        s_db_available = false;
    }
    return false;
}

/* If a pre-v2 schema is detected (table `measurements` from the float-only era),
 * archive the entire DB file out of the way and start fresh. Bytes are
 * preserved on disk under a timestamped filename for manual recovery. */
static esp_err_t maybe_archive_legacy_db(void)
{
    /* Need to open the file briefly just to detect what's inside. */
    sqlite3 *probe = NULL;
    if (sqlite3_open(DB_PATH, &probe) != SQLITE_OK) {
        if (probe) sqlite3_close(probe);
        return ESP_OK; /* fresh file — nothing to archive */
    }

    int version = read_user_version(probe);
    bool has_legacy = table_exists(probe, "measurements");
    bool has_v2     = table_exists(probe, "measurements_v2");
    sqlite3_close(probe);

    if (version >= SCHEMA_VERSION || has_v2) {
        return ESP_OK; /* current schema present — no migration needed */
    }
    if (!has_legacy) {
        return ESP_OK; /* fresh DB without our tables — let CREATE TABLE handle it */
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    char archive_path[96];
    snprintf(archive_path, sizeof(archive_path),
             DB_PATH ".legacy_%lld", (long long)tv.tv_sec);

    if (rename(DB_PATH, archive_path) != 0) {
        ESP_LOGW(TAG, "Failed to archive legacy DB; removing instead");
        remove(DB_PATH);
    } else {
        ESP_LOGW(TAG, "Legacy schema archived to %s", archive_path);
    }

    /* Also move WAL / journal sidecars out of the way so the fresh open is clean. */
    rename(DB_PATH "-wal",      DB_PATH "-wal.legacy");
    rename(DB_PATH "-shm",      DB_PATH "-shm.legacy");
    rename(DB_PATH "-journal",  DB_PATH "-journal.legacy");
    return ESP_OK;
}

static esp_err_t db_open_and_configure(void)
{
    int rc = sqlite3_open(DB_PATH, &s_db);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "sqlite3_open failed: %s", sqlite3_errmsg(s_db));
        s_db = NULL;
        return ESP_FAIL;
    }

    /* Integrity check */
    bool integrity_ok = true;
    char *err_msg = NULL;
    rc = sqlite3_exec(s_db, "PRAGMA integrity_check;", integrity_callback,
                      &integrity_ok, &err_msg);
    sqlite3_free(err_msg);

    if (!integrity_ok || rc != SQLITE_OK) {
        ESP_LOGE(TAG, "DB integrity check failed, recovering");
        sqlite3_close(s_db);
        s_db = NULL;

        rename(DB_PATH, DB_PATH ".corrupt");
        rename(DB_PATH "-wal", DB_PATH "-wal.corrupt");
        rename(DB_PATH "-shm", DB_PATH "-shm.corrupt");
        rename(DB_PATH "-journal", DB_PATH "-journal.corrupt");
        increment_nvs_counter("db_corrupt_cnt");

        rc = sqlite3_open(DB_PATH, &s_db);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "Re-open failed: %s", sqlite3_errmsg(s_db));
            s_db = NULL;
            return ESP_FAIL;
        }
    }

    /* Extended result codes give us SQLITE_IOERR_* sub-causes instead of just
     * SQLITE_IOERR=10. Set before any prepare/step. */
    sqlite3_extended_result_codes(s_db, 1);

    /* PRAGMAs — crash-safe configuration for SD card */
    exec_pragma(s_db, "PRAGMA journal_mode = TRUNCATE;");
    exec_pragma(s_db, "PRAGMA locking_mode = EXCLUSIVE;");
    exec_pragma(s_db, "PRAGMA synchronous = FULL;");
    exec_pragma(s_db, "PRAGMA page_size = 4096;");
    exec_pragma(s_db, "PRAGMA cache_size = -64;");
    exec_pragma(s_db, "PRAGMA temp_store = MEMORY;");

    /* Schema (v2) */
    const char *create_table =
        "CREATE TABLE IF NOT EXISTS measurements_v2 ("
        "measure_id  INTEGER NOT NULL, "
        "quantity    TEXT    NOT NULL, "
        "start_ticks INTEGER NOT NULL, "
        "end_ticks   INTEGER NOT NULL, "
        "device      TEXT, "
        "sensor      TEXT    NOT NULL, "
        "sensor_id   INTEGER, "
        "metadata    TEXT, "
        "value_real  REAL, "
        "value_text  TEXT, "
        "sync_state  INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY (measure_id, quantity), "
        "CHECK ( (value_real IS NULL) <> (value_text IS NULL) ));";
    rc = sqlite3_exec(s_db, create_table, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "CREATE TABLE failed: %s", err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        sqlite3_close(s_db);
        s_db = NULL;
        return ESP_FAIL;
    }
    sqlite3_free(err_msg);

    const char *create_idx =
        "CREATE INDEX IF NOT EXISTS idx_v2_sync_start "
        "ON measurements_v2 (sync_state, start_ticks);";
    err_msg = NULL;
    rc = sqlite3_exec(s_db, create_idx, NULL, NULL, &err_msg);
    sqlite3_free(err_msg);

    /* Anchor for future migrations */
    char pragma[64];
    snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d;", SCHEMA_VERSION);
    exec_pragma(s_db, pragma);

    /* Crash-recovery: anything left INFLIGHT by a previous interrupted session
     * goes back to PENDING. Single statement, transactional. */
    err_msg = NULL;
    rc = sqlite3_exec(s_db,
                      "UPDATE measurements_v2 SET sync_state = 0 WHERE sync_state = 1;",
                      NULL, NULL, &err_msg);
    if (rc == SQLITE_OK) {
        int n = sqlite3_changes(s_db);
        if (n > 0) {
            ESP_LOGW(TAG, "Crash recovery: reset %d INFLIGHT rows to PENDING", n);
        }
    } else {
        ESP_LOGW(TAG, "Crash-recovery UPDATE failed: %s", err_msg ? err_msg : "");
    }
    sqlite3_free(err_msg);

    return ESP_OK;
}

static int64_t db_get_max_measure_id(void)
{
    if (s_db == NULL) {
        return 0;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, "SELECT MAX(measure_id) FROM measurements_v2;",
                                -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    int64_t max_id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        max_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return max_id;
}

/* ── row <-> SQL marshalling ─────────────────────────────────────────── */

/* Populate `r` from the current row of `stmt`. Column order must match the
 * SELECT list used by all reader queries below. */
static void read_row(sqlite3_stmt *stmt, measurement_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->measure_id      = sqlite3_column_int64(stmt, 0);
    const unsigned char *q = sqlite3_column_text(stmt, 1);
    if (q) strncpy(r->quantity, (const char *)q, sizeof(r->quantity) - 1);
    r->start_ticks_ms  = sqlite3_column_int64(stmt, 2);
    r->end_ticks_ms    = sqlite3_column_int64(stmt, 3);
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
        const unsigned char *d = sqlite3_column_text(stmt, 4);
        if (d) strncpy(r->device, (const char *)d, sizeof(r->device) - 1);
    }
    const unsigned char *s = sqlite3_column_text(stmt, 5);
    if (s) strncpy(r->sensor, (const char *)s, sizeof(r->sensor) - 1);
    r->sensor_id = (sqlite3_column_type(stmt, 6) == SQLITE_NULL)
                   ? MEASUREMENT_SENSOR_ID_NONE
                   : sqlite3_column_int64(stmt, 6);
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
        const unsigned char *m = sqlite3_column_text(stmt, 7);
        if (m) strncpy(r->metadata, (const char *)m, sizeof(r->metadata) - 1);
    }
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        r->value_is_string = false;
        r->value_real      = (float)sqlite3_column_double(stmt, 8);
    } else {
        r->value_is_string = true;
        const unsigned char *v = sqlite3_column_text(stmt, 9);
        if (v) strncpy(r->value_text, (const char *)v, sizeof(r->value_text) - 1);
    }
    r->sync_state = (measurement_sync_state_t)sqlite3_column_int(stmt, 10);
}

#define READ_COLUMNS \
    "measure_id, quantity, start_ticks, end_ticks, device, sensor, " \
    "sensor_id, metadata, value_real, value_text, sync_state"

/* Bind one record into a prepared INSERT (column order matches insert_sql below). */
static void bind_record(sqlite3_stmt *stmt, const measurement_record_t *r)
{
    sqlite3_bind_int64(stmt, 1, r->measure_id);
    sqlite3_bind_text (stmt, 2, r->quantity, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, r->start_ticks_ms);
    sqlite3_bind_int64(stmt, 4, r->end_ticks_ms);
    if (r->device[0] == '\0') {
        sqlite3_bind_null(stmt, 5);
    } else {
        sqlite3_bind_text(stmt, 5, r->device, -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text (stmt, 6, r->sensor, -1, SQLITE_TRANSIENT);
    if (r->sensor_id == MEASUREMENT_SENSOR_ID_NONE) {
        sqlite3_bind_null(stmt, 7);
    } else {
        sqlite3_bind_int64(stmt, 7, r->sensor_id);
    }
    if (r->metadata[0] == '\0') {
        sqlite3_bind_null(stmt, 8);
    } else {
        sqlite3_bind_text(stmt, 8, r->metadata, -1, SQLITE_TRANSIENT);
    }
    if (r->value_is_string) {
        sqlite3_bind_null(stmt, 9);
        sqlite3_bind_text(stmt, 10, r->value_text, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_double(stmt, 9, (double)r->value_real);
        sqlite3_bind_null  (stmt, 10);
    }
    sqlite3_bind_int(stmt, 11, (int)r->sync_state);
}

/* ── flush logic ─────────────────────────────────────────────────────── */

static esp_err_t flush_batch_to_sqlite(const pending_entry_t *entries, size_t count)
{
    if (s_db == NULL || count == 0) {
        return ESP_FAIL;
    }

    char *err_msg = NULL;
    int rc = sqlite3_exec(s_db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "BEGIN TRANSACTION failed (%d): %s",
                 rc, err_msg ? err_msg : sqlite3_errmsg(s_db));
        sqlite3_free(err_msg);
        return ESP_FAIL;
    }
    sqlite3_free(err_msg);

    const char *insert_sql =
        "INSERT OR REPLACE INTO measurements_v2 "
        "(measure_id, quantity, start_ticks, end_ticks, device, sensor, "
        " sensor_id, metadata, value_real, value_text, sync_state) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(s_db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "prepare_v2 failed (%d): %s", rc, sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        return ESP_FAIL;
    }

    size_t inserted = 0;
    for (size_t i = 0; i < count; i++) {
        bind_record(stmt, &entries[i].record);

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            inserted++;
        } else {
            ESP_LOGW(TAG, "INSERT failed for measure_id %lld / quantity %s: %s",
                     (long long)entries[i].record.measure_id,
                     entries[i].record.quantity,
                     sqlite3_errmsg(s_db));
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    rc = sqlite3_exec(s_db, "COMMIT;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "COMMIT failed (%d): %s",
                 rc, err_msg ? err_msg : sqlite3_errmsg(s_db));
        sqlite3_free(err_msg);
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        return ESP_FAIL;
    }
    sqlite3_free(err_msg);
    ESP_LOGI(TAG, "flush: %u/%u rows committed", (unsigned)inserted, (unsigned)count);
    return ESP_OK;
}

static void drain_pending_to_sqlite(void)
{
    if (!s_db_available) {
        return;
    }

    /* One heap-allocated buffer per drain, reused across batches. With the new
     * measurement_record_t the entries buffer is ~6 KiB — too big to live on
     * the task stack (see FLUSH_TASK_STACK note). */
    pending_entry_t *entries = calloc(FLUSH_BATCH_SIZE, sizeof(*entries));
    if (entries == NULL) {
        ESP_LOGE(TAG, "drain: entries buffer alloc failed (%u bytes)",
                 (unsigned)(FLUSH_BATCH_SIZE * sizeof(*entries)));
        return;
    }

    for (;;) {
        size_t pending = pending_store_count();
        if (pending == 0) {
            break;
        }

        size_t batch = (pending < FLUSH_BATCH_SIZE) ? pending : FLUSH_BATCH_SIZE;
        size_t read_count = 0;

        if (pending_store_read(entries, batch, &read_count) != ESP_OK || read_count == 0) {
            break;
        }
        ESP_LOGI(TAG, "drain: pulled %u entries from ring (still %u in ring)",
                 (unsigned)read_count, (unsigned)(pending - read_count));

        if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            break;
        }

        if (!sdcard_is_mounted()) {
            xSemaphoreGive(s_sqlite_mutex);
            ESP_LOGW(TAG, "SD unmounted during flush");
            break;
        }

        esp_err_t err = flush_batch_to_sqlite(entries, read_count);

        if (err == ESP_OK) {
            s_flush_errors = 0;
            if (++s_flush_cycles >= INTEGRITY_CHECK_INTERVAL) {
                s_flush_cycles = 0;
                if (!run_quick_check_locked()) {
                    xSemaphoreGive(s_sqlite_mutex);
                    break;
                }
            }
            xSemaphoreGive(s_sqlite_mutex);
            pending_store_remove(read_count);
        } else {
            s_flush_errors++;
            ESP_LOGW(TAG, "Flush failed (%d consecutive)", s_flush_errors);

            if (s_flush_errors >= FLUSH_ERROR_DISABLE_THRESHOLD) {
                ESP_LOGE(TAG, "Too many flush errors — disabling DB until reboot");
                s_db_available = false;
                xSemaphoreGive(s_sqlite_mutex);
                break;
            }
            if (s_flush_errors >= FLUSH_ERROR_CHECK_THRESHOLD) {
                ESP_LOGW(TAG, "Running integrity check after %d errors", s_flush_errors);
                if (!run_quick_check_locked()) {
                    s_flush_errors = 0;
                }
            }
            xSemaphoreGive(s_sqlite_mutex);
            break;
        }
    }

    free(entries);
}

static void flush_task(void *arg)
{
    (void)arg;

    drain_pending_to_sqlite();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FLUSH_INTERVAL_MS));

        if (!sdcard_is_mounted()) {
            continue;
        }

        drain_pending_to_sqlite();
    }
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t sqlite_persistence_init(void)
{
    s_sqlite_mutex = xSemaphoreCreateMutexStatic(&s_sqlite_mutex_storage);
    if (s_sqlite_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = pending_store_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pending store init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (sdcard_is_mounted()) {
        sqlite3_initialize();
        /* Archive pre-v2 schemas before opening for real. */
        maybe_archive_legacy_db();

        err = db_open_and_configure();
        if (err == ESP_OK) {
            s_db_available = true;
            int64_t sqlite_max = db_get_max_measure_id();
            pending_store_seed_max_id(sqlite_max);
            ESP_LOGI(TAG, "SQLite DB ready (schema v%d), max measure_id = %lld",
                     (int)SCHEMA_VERSION, (long long)sqlite_max);
        } else {
            ESP_LOGW(TAG, "SQLite unavailable, buffering to LittleFS only");
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted, buffering to LittleFS only");
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        flush_task, "persist_flush", FLUSH_TASK_STACK, NULL, 2, &s_flush_task, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create flush task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Persistence layer initialized");
    return ESP_OK;
}

esp_err_t sqlite_persistence_store(const measurement_record_t *records, size_t count)
{
    return pending_store_append(records, count);
}

esp_err_t sqlite_persistence_query(const char *quantity, int64_t from_ms, int64_t to_ms,
                                    measurement_record_t *out, size_t max, size_t *count)
{
    if (quantity == NULL || out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        *count = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "SELECT " READ_COLUMNS " FROM measurements_v2 "
        "WHERE quantity = ? AND start_ticks BETWEEN ? AND ? "
        "ORDER BY measure_id, quantity;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text (stmt, 1, quantity, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, from_ms);
    sqlite3_bind_int64(stmt, 3, to_ms);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        read_row(stmt, &out[n]);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_count(const char *quantity, size_t *count)
{
    if (quantity == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        *count = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql = "SELECT COUNT(DISTINCT measure_id) FROM measurements_v2 WHERE quantity = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, quantity, -1, SQLITE_STATIC);

    *count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *count = (size_t)sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_next_id(int64_t *out_id)
{
    return pending_store_next_id(out_id);
}

esp_err_t sqlite_persistence_query_unsynced(const char *quantity,
                                             measurement_record_t *out, size_t max,
                                             size_t *count)
{
    if (quantity == NULL || out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        *count = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "SELECT " READ_COLUMNS " FROM measurements_v2 "
        "WHERE quantity = ? AND sync_state = 0 "
        "ORDER BY measure_id, quantity;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, quantity, -1, SQLITE_STATIC);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        read_row(stmt, &out[n]);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_query_by_id(int64_t measure_id,
                                          measurement_record_t *out, size_t max,
                                          size_t *count)
{
    if (out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        *count = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "SELECT " READ_COLUMNS " FROM measurements_v2 "
        "WHERE measure_id = ? ORDER BY quantity;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, measure_id);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        read_row(stmt, &out[n]);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

/* ── batch claim / mark ─────────────────────────────────────────────── */

esp_err_t sqlite_persistence_claim_next_pending_batch(measurement_record_t *out,
                                                       size_t max_rows,
                                                       size_t *out_count)
{
    if (out == NULL || out_count == NULL || max_rows == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char *err = NULL;
    int rc = sqlite3_exec(s_db, "BEGIN IMMEDIATE;", NULL, NULL, &err);
    sqlite3_free(err);
    err = NULL;
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    /* 1. Find the oldest pending measure_id. No GROUP BY / no aggregates -
     * an index walk on the PK is the only cost. The previous GROUP BY query
     * triggered an internal temp B-tree which the vendored SQLite 3.25.2
     * tries to write to disk even with temp_store=MEMORY, returning
     * SQLITE_IOERR on the SD. measure_id is monotonic from pending_store, so
     * ORDER BY measure_id ASC is "oldest first". */
    const char *candidate_sql =
        "SELECT measure_id FROM measurements_v2 "
        "WHERE sync_state = 0 "
        "ORDER BY measure_id "
        "LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(s_db, candidate_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "claim_batch candidate prepare failed (%d/%d): %s",
                 rc, sqlite3_extended_errcode(s_db), sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    int64_t claimed_id = -1;
    int step_rc = sqlite3_step(stmt);
    if (step_rc == SQLITE_ROW) {
        claimed_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (claimed_id < 0) {
        if (step_rc != SQLITE_DONE) {
            ESP_LOGW(TAG, "claim_batch: candidate step_rc=%d/%d: %s",
                     step_rc, sqlite3_extended_errcode(s_db), sqlite3_errmsg(s_db));
        }
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. Mark all rows of that group INFLIGHT. */
    const char *update_sql =
        "UPDATE measurements_v2 SET sync_state = 1 "
        "WHERE measure_id = ? AND sync_state = 0;";
    rc = sqlite3_prepare_v2(s_db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "claim_batch update prepare failed (%d/%d): %s",
                 rc, sqlite3_extended_errcode(s_db), sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }
    sqlite3_bind_int64(stmt, 1, claimed_id);
    int upd_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (upd_rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "claim_batch update step failed (%d/%d): %s",
                 upd_rc, sqlite3_extended_errcode(s_db), sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    /* 3. Read back the claimed rows. */
    const char *select_sql =
        "SELECT " READ_COLUMNS " FROM measurements_v2 "
        "WHERE measure_id = ? AND sync_state = 1 "
        "ORDER BY quantity;";
    rc = sqlite3_prepare_v2(s_db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "claim_batch select prepare failed (%d/%d): %s",
                 rc, sqlite3_extended_errcode(s_db), sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }
    sqlite3_bind_int64(stmt, 1, claimed_id);

    size_t n = 0;
    while (n < max_rows && sqlite3_step(stmt) == SQLITE_ROW) {
        read_row(stmt, &out[n]);
        n++;
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(s_db, "COMMIT;", NULL, NULL, &err);
    sqlite3_free(err);
    xSemaphoreGive(s_sqlite_mutex);

    if (rc != SQLITE_OK) {
        return ESP_FAIL;
    }

    *out_count = n;
    return ESP_OK;
}

static esp_err_t mark_batch_state(const int64_t *measure_ids, size_t count, int new_state)
{
    if (measure_ids == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char *err = NULL;
    int rc = sqlite3_exec(s_db, "BEGIN TRANSACTION;", NULL, NULL, &err);
    sqlite3_free(err);
    err = NULL;
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    const char *sql =
        "UPDATE measurements_v2 SET sync_state = ? "
        "WHERE measure_id = ? AND sync_state = 1;";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; i++) {
        sqlite3_bind_int  (stmt, 1, new_state);
        sqlite3_bind_int64(stmt, 2, measure_ids[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
            xSemaphoreGive(s_sqlite_mutex);
            return ESP_FAIL;
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(s_db, "COMMIT;", NULL, NULL, &err);
    sqlite3_free(err);
    xSemaphoreGive(s_sqlite_mutex);
    return (rc == SQLITE_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t sqlite_persistence_mark_batch_synced(const int64_t *measure_ids, size_t count)
{
    return mark_batch_state(measure_ids, count, (int)MEASUREMENT_SYNC_SYNCED);
}

esp_err_t sqlite_persistence_mark_batch_pending(const int64_t *measure_ids, size_t count)
{
    return mark_batch_state(measure_ids, count, (int)MEASUREMENT_SYNC_PENDING);
}

/* ── fn getters ──────────────────────────────────────────────────────── */

measurement_store_fn sqlite_persistence_get_store_fn(void)               { return sqlite_persistence_store; }
measurement_query_fn sqlite_persistence_get_query_fn(void)               { return sqlite_persistence_query; }
measurement_count_fn sqlite_persistence_get_count_fn(void)               { return sqlite_persistence_count; }
measurement_next_id_fn sqlite_persistence_get_next_id_fn(void)           { return sqlite_persistence_next_id; }
measurement_query_unsynced_fn sqlite_persistence_get_query_unsynced_fn(void) { return sqlite_persistence_query_unsynced; }
measurement_query_by_id_fn sqlite_persistence_get_query_by_id_fn(void)   { return sqlite_persistence_query_by_id; }
measurement_claim_next_pending_batch_fn sqlite_persistence_get_claim_next_pending_batch_fn(void) { return sqlite_persistence_claim_next_pending_batch; }
measurement_mark_batch_synced_fn sqlite_persistence_get_mark_batch_synced_fn(void) { return sqlite_persistence_mark_batch_synced; }
measurement_mark_batch_pending_fn sqlite_persistence_get_mark_batch_pending_fn(void) { return sqlite_persistence_mark_batch_pending; }
