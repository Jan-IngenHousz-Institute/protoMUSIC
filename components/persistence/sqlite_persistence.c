#include "sqlite_persistence.h"
#include "pending_store.h"
#include "sd_card.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sqlite3.h"

#define TAG "sqlite_persist"
#define DB_PATH "/sdcard/measurements.db"
#define FLUSH_INTERVAL_MS 5000
#define FLUSH_BATCH_SIZE  16
#define FLUSH_TASK_STACK  6144
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

/* Ensure the 'syncState' column exists, migrating from legacy names if needed */
static void migrate_schema(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, "PRAGMA table_info(measurements);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return;
    }

    bool has_synced    = false;
    bool has_syncState = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *col = (const char *)sqlite3_column_text(stmt, 1);
        if (col == NULL) continue;
        if (strcmp(col, "synced")    == 0) has_synced    = true;
        if (strcmp(col, "syncState") == 0) has_syncState = true;
    }
    sqlite3_finalize(stmt);

    if (has_syncState) {
        return; /* already correct */
    }

    char *err = NULL;
    if (has_synced) {
        rc = sqlite3_exec(s_db,
                          "ALTER TABLE measurements RENAME COLUMN synced TO syncState;",
                          NULL, NULL, &err);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "Schema migration (rename) failed: %s", err ? err : "");
        } else {
            ESP_LOGI(TAG, "Migrated schema: column 'synced' -> 'syncState'");
            sqlite3_free(err);
            return;
        }
        sqlite3_free(err);
        err = NULL;
    }

    /* syncState missing and rename not possible — add the column */
    rc = sqlite3_exec(s_db,
                      "ALTER TABLE measurements ADD COLUMN syncState INTEGER NOT NULL DEFAULT 0;",
                      NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Schema migration (add column) failed: %s", err ? err : "");
    } else {
        ESP_LOGI(TAG, "Schema migration: added 'syncState' column with DEFAULT 0");
    }
    sqlite3_free(err);
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

    /* PRAGMAs — crash-safe configuration for SD card
     *
     * TRUNCATE mode: journal file is truncated to zero on commit (not deleted).
     * Avoids FAT32 directory entry create/delete churn on every transaction.
     * On crash: SQLite replays the journal on next open → DB rolled back cleanly.
     *
     * synchronous = FULL: journal is fsynced to SD before any main DB page is
     * modified. This is the cornerstone of crash safety — without it, a power
     * loss can leave the DB with half-written pages and no valid journal. */
    exec_pragma(s_db, "PRAGMA journal_mode = TRUNCATE;");
    exec_pragma(s_db, "PRAGMA locking_mode = EXCLUSIVE;");
    exec_pragma(s_db, "PRAGMA synchronous = FULL;");
    exec_pragma(s_db, "PRAGMA page_size = 4096;");
    exec_pragma(s_db, "PRAGMA cache_size = -64;");
    exec_pragma(s_db, "PRAGMA temp_store = MEMORY;");

    /* Schema */
    const char *create_table =
        "CREATE TABLE IF NOT EXISTS measurements ("
        "sensorID INTEGER NOT NULL, "
        "measureID INTEGER NOT NULL, "
        "measureType TEXT NOT NULL, "
        "timestamp INTEGER NOT NULL, "
        "dataType TEXT NOT NULL, "
        "dataValue REAL, "
        "syncState INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY (measureID, dataType));";

    rc = sqlite3_exec(s_db, create_table, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "CREATE TABLE failed: %s", err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        sqlite3_close(s_db);
        s_db = NULL;
        return ESP_FAIL;
    }

    const char *create_idx_type_ts =
        "CREATE INDEX IF NOT EXISTS idx_type_ts ON measurements (measureType, timestamp);";
    rc = sqlite3_exec(s_db, create_idx_type_ts, NULL, NULL, &err_msg);
    sqlite3_free(err_msg);

    const char *create_idx_sync =
        "CREATE INDEX IF NOT EXISTS idx_sync_type "
        "ON measurements (syncState, measureType, timestamp);";
    rc = sqlite3_exec(s_db, create_idx_sync, NULL, NULL, &err_msg);
    sqlite3_free(err_msg);

    /* Migrate old 'synced' column if present from a previous schema version */
    migrate_schema();

    /* Reset stale INFLIGHT rows left by a previous interrupted session */
    rc = sqlite3_exec(s_db,
                      "UPDATE measurements SET syncState = 0 WHERE syncState = 1;",
                      NULL, NULL, &err_msg);
    sqlite3_free(err_msg);

    return ESP_OK;
}

static int64_t db_get_max_measure_id(void)
{
    if (s_db == NULL) {
        return 0;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, "SELECT MAX(measureID) FROM measurements;",
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
        "INSERT OR REPLACE INTO measurements "
        "(sensorID, measureID, measureType, timestamp, dataType, dataValue, syncState) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(s_db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "prepare_v2 failed (%d): %s", rc, sqlite3_errmsg(s_db));
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; i++) {
        const measurement_record_t *r = &entries[i].record;

        sqlite3_bind_int64(stmt, 1, r->sensor_id);
        sqlite3_bind_int64(stmt, 2, r->measure_id);
        sqlite3_bind_text(stmt, 3, r->measure_type, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, (int64_t)r->timestamp);
        sqlite3_bind_text(stmt, 5, r->data_type, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 6, (double)r->value);
        sqlite3_bind_int(stmt, 7, (int)r->sync_state);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            ESP_LOGW(TAG, "INSERT failed for measure_id %lld: %s",
                     (long long)r->measure_id, sqlite3_errmsg(s_db));
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
    return ESP_OK;
}

static void drain_pending_to_sqlite(void)
{
    if (!s_db_available) {
        return;
    }

    for (;;) {
        size_t pending = pending_store_count();
        if (pending == 0) {
            break;
        }

        size_t batch = (pending < FLUSH_BATCH_SIZE) ? pending : FLUSH_BATCH_SIZE;
        pending_entry_t entries[FLUSH_BATCH_SIZE];
        size_t read_count = 0;

        if (pending_store_read(entries, batch, &read_count) != ESP_OK || read_count == 0) {
            break;
        }

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

            /* Periodic integrity check (~every 8 minutes) */
            if (++s_flush_cycles >= INTEGRITY_CHECK_INTERVAL) {
                s_flush_cycles = 0;
                if (!run_quick_check_locked()) {
                    xSemaphoreGive(s_sqlite_mutex);
                    break;  /* DB was recreated — retry from fresh state next cycle */
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
                    s_flush_errors = 0;  /* DB was recreated, reset counter */
                }
            }
            xSemaphoreGive(s_sqlite_mutex);
            break;  /* retry next cycle */
        }
    }
}

static void flush_task(void *arg)
{
    (void)arg;

    /* Startup drain */
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

    /* Open SQLite if SD card is mounted */
    if (sdcard_is_mounted()) {
        sqlite3_initialize();
        err = db_open_and_configure();
        if (err == ESP_OK) {
            s_db_available = true;
            int64_t sqlite_max = db_get_max_measure_id();
            pending_store_seed_max_id(sqlite_max);
            ESP_LOGI(TAG, "SQLite DB ready, max measureID = %lld",
                     (long long)sqlite_max);
        } else {
            ESP_LOGW(TAG, "SQLite unavailable, buffering to LittleFS only");
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted, buffering to LittleFS only");
    }

    /* Start flush task */
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

esp_err_t sqlite_persistence_query(const char *measure_type, time_t from, time_t to,
                                   measurement_record_t *out, size_t max, size_t *count)
{
    if (measure_type == NULL || out == NULL || count == NULL) {
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
        "SELECT sensorID, measureID, measureType, timestamp, dataType, dataValue, syncState "
        "FROM measurements WHERE measureType = ? AND timestamp BETWEEN ? AND ? "
        "ORDER BY measureID, dataType;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, measure_type, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (int64_t)from);
    sqlite3_bind_int64(stmt, 3, (int64_t)to);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        measurement_record_t *r = &out[n];
        r->sensor_id = sqlite3_column_int64(stmt, 0);
        r->measure_id = sqlite3_column_int64(stmt, 1);
        strncpy(r->measure_type, (const char *)sqlite3_column_text(stmt, 2),
                sizeof(r->measure_type) - 1);
        r->measure_type[sizeof(r->measure_type) - 1] = '\0';
        r->timestamp = (time_t)sqlite3_column_int64(stmt, 3);
        strncpy(r->data_type, (const char *)sqlite3_column_text(stmt, 4),
                sizeof(r->data_type) - 1);
        r->data_type[sizeof(r->data_type) - 1] = '\0';
        r->value = (float)sqlite3_column_double(stmt, 5);
        r->sync_state = (measurement_sync_state_t)sqlite3_column_int(stmt, 6);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_count(const char *measure_type, size_t *count)
{
    if (measure_type == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_db_available) {
        *count = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql = "SELECT COUNT(DISTINCT measureID) FROM measurements WHERE measureType = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, measure_type, -1, SQLITE_STATIC);

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

esp_err_t sqlite_persistence_query_unsynced(const char *measure_type,
                                            measurement_record_t *out, size_t max,
                                            size_t *count)
{
    if (measure_type == NULL || out == NULL || count == NULL) {
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
        "SELECT sensorID, measureID, measureType, timestamp, dataType, dataValue, syncState "
        "FROM measurements WHERE measureType = ? AND syncState = 0 "
        "ORDER BY measureID, dataType;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, measure_type, -1, SQLITE_STATIC);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        measurement_record_t *r = &out[n];
        r->sensor_id = sqlite3_column_int64(stmt, 0);
        r->measure_id = sqlite3_column_int64(stmt, 1);
        strncpy(r->measure_type, (const char *)sqlite3_column_text(stmt, 2),
                sizeof(r->measure_type) - 1);
        r->measure_type[sizeof(r->measure_type) - 1] = '\0';
        r->timestamp = (time_t)sqlite3_column_int64(stmt, 3);
        strncpy(r->data_type, (const char *)sqlite3_column_text(stmt, 4),
                sizeof(r->data_type) - 1);
        r->data_type[sizeof(r->data_type) - 1] = '\0';
        r->value = (float)sqlite3_column_double(stmt, 5);
        r->sync_state = (measurement_sync_state_t)sqlite3_column_int(stmt, 6);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_mark_synced(int64_t measure_id)
{
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "UPDATE measurements SET syncState = 2 WHERE measureID = ? AND syncState = 1;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, measure_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_sqlite_mutex);

    return (rc == SQLITE_DONE) ? ESP_OK : ESP_FAIL;
}

esp_err_t sqlite_persistence_mark_inflight(int64_t measure_id)
{
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "UPDATE measurements SET syncState = 1 WHERE measureID = ? AND syncState = 0;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, measure_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_sqlite_mutex);

    return (rc == SQLITE_DONE) ? ESP_OK : ESP_FAIL;
}

esp_err_t sqlite_persistence_mark_pending(int64_t measure_id)
{
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *sql =
        "UPDATE measurements SET syncState = 0 WHERE measureID = ? AND syncState = 1;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, measure_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_sqlite_mutex);

    return (rc == SQLITE_DONE) ? ESP_OK : ESP_FAIL;
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
        "SELECT sensorID, measureID, measureType, timestamp, dataType, dataValue, syncState "
        "FROM measurements WHERE measureID = ? ORDER BY dataType;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, measure_id);

    size_t n = 0;
    while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
        measurement_record_t *r = &out[n];
        r->sensor_id = sqlite3_column_int64(stmt, 0);
        r->measure_id = sqlite3_column_int64(stmt, 1);
        strncpy(r->measure_type, (const char *)sqlite3_column_text(stmt, 2),
                sizeof(r->measure_type) - 1);
        r->measure_type[sizeof(r->measure_type) - 1] = '\0';
        r->timestamp = (time_t)sqlite3_column_int64(stmt, 3);
        strncpy(r->data_type, (const char *)sqlite3_column_text(stmt, 4),
                sizeof(r->data_type) - 1);
        r->data_type[sizeof(r->data_type) - 1] = '\0';
        r->value = (float)sqlite3_column_double(stmt, 5);
        r->sync_state = (measurement_sync_state_t)sqlite3_column_int(stmt, 6);
        n++;
    }

    sqlite3_finalize(stmt);
    *count = n;
    xSemaphoreGive(s_sqlite_mutex);
    return ESP_OK;
}

esp_err_t sqlite_persistence_claim_next_pending(const char *measure_type,
                                                  int64_t *out_measure_id)
{
    if (measure_type == NULL || out_measure_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_db_available) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_sqlite_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char *err = NULL;
    int rc = sqlite3_exec(s_db, "BEGIN IMMEDIATE;", NULL, NULL, &err);
    sqlite3_free(err);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    /* Find the oldest PENDING group for this measure_type */
    const char *select_sql =
        "SELECT measureID FROM measurements "
        "WHERE measureType = ? AND syncState = 0 "
        "ORDER BY timestamp, measureID LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(s_db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_text(stmt, 1, measure_type, -1, SQLITE_STATIC);

    int64_t claimed_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        claimed_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (claimed_id < 0) {
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    /* Mark all rows of this group INFLIGHT atomically */
    const char *update_sql =
        "UPDATE measurements SET syncState = 1 WHERE measureID = ? AND syncState = 0;";
    rc = sqlite3_prepare_v2(s_db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    sqlite3_bind_int64(stmt, 1, claimed_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(s_db, "ROLLBACK;", NULL, NULL, NULL);
        xSemaphoreGive(s_sqlite_mutex);
        return ESP_FAIL;
    }

    rc = sqlite3_exec(s_db, "COMMIT;", NULL, NULL, &err);
    sqlite3_free(err);
    xSemaphoreGive(s_sqlite_mutex);

    if (rc != SQLITE_OK) {
        return ESP_FAIL;
    }

    *out_measure_id = claimed_id;
    return ESP_OK;
}

/* ── fn getters ──────────────────────────────────────────────────────── */

measurement_store_fn sqlite_persistence_get_store_fn(void)
{
    return sqlite_persistence_store;
}

measurement_query_fn sqlite_persistence_get_query_fn(void)
{
    return sqlite_persistence_query;
}

measurement_count_fn sqlite_persistence_get_count_fn(void)
{
    return sqlite_persistence_count;
}

measurement_next_id_fn sqlite_persistence_get_next_id_fn(void)
{
    return sqlite_persistence_next_id;
}

measurement_query_unsynced_fn sqlite_persistence_get_query_unsynced_fn(void)
{
    return sqlite_persistence_query_unsynced;
}

measurement_mark_synced_fn sqlite_persistence_get_mark_synced_fn(void)
{
    return sqlite_persistence_mark_synced;
}

measurement_mark_inflight_fn sqlite_persistence_get_mark_inflight_fn(void)
{
    return sqlite_persistence_mark_inflight;
}

measurement_mark_pending_fn sqlite_persistence_get_mark_pending_fn(void)
{
    return sqlite_persistence_mark_pending;
}

measurement_query_by_id_fn sqlite_persistence_get_query_by_id_fn(void)
{
    return sqlite_persistence_query_by_id;
}

measurement_claim_next_pending_fn sqlite_persistence_get_claim_next_pending_fn(void)
{
    return sqlite_persistence_claim_next_pending;
}
