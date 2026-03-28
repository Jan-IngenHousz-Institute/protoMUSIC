#include "pending_store.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "pending_store"
#define PENDING_DATA_PATH  "/littlefs/pending.bin"
#define PENDING_META_PATH  "/littlefs/pending_meta.bin"
#define PENDING_CAPACITY_WARN_PCT 80

typedef struct {
    uint32_t head;
    uint32_t tail;
    int64_t  max_measure_id;
} pending_meta_t;

static SemaphoreHandle_t s_pending_mutex = NULL;
static StaticSemaphore_t s_pending_mutex_storage;
static pending_meta_t s_meta = {0};
static int64_t s_next_measure_id = 1;
static bool s_initialized = false;

static uint32_t compute_entry_crc(const pending_entry_t *entry)
{
    /* CRC over magic + record, excluding the crc32 field itself */
    return esp_crc32_le(0, (const uint8_t *)entry,
                        offsetof(pending_entry_t, crc32));
}

static esp_err_t meta_load(void)
{
    FILE *f = fopen(PENDING_META_PATH, "rb");
    if (f == NULL) {
        memset(&s_meta, 0, sizeof(s_meta));
        return ESP_OK;
    }

    size_t n = fread(&s_meta, sizeof(s_meta), 1, f);
    fclose(f);

    if (n != 1) {
        ESP_LOGW(TAG, "Corrupt metadata, resetting");
        memset(&s_meta, 0, sizeof(s_meta));
    }

    return ESP_OK;
}

static esp_err_t meta_save(void)
{
    FILE *f = fopen(PENDING_META_PATH, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Cannot open metadata for writing");
        return ESP_FAIL;
    }

    size_t n = fwrite(&s_meta, sizeof(s_meta), 1, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return (n == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t pending_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_pending_mutex = xSemaphoreCreateMutexStatic(&s_pending_mutex_storage);
    if (s_pending_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    meta_load();
    s_next_measure_id = s_meta.max_measure_id + 1;
    s_initialized = true;

    ESP_LOGI(TAG, "Initialized: head=%lu tail=%lu max_id=%lld",
             (unsigned long)s_meta.head, (unsigned long)s_meta.tail,
             (long long)s_meta.max_measure_id);
    return ESP_OK;
}

void pending_store_seed_max_id(int64_t sqlite_max_id)
{
    if (sqlite_max_id >= s_next_measure_id) {
        s_next_measure_id = sqlite_max_id + 1;
        if (sqlite_max_id > s_meta.max_measure_id) {
            s_meta.max_measure_id = sqlite_max_id;
        }
    }
}

esp_err_t pending_store_next_id(int64_t *out_id)
{
    if (out_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out_id = s_next_measure_id++;
    if (s_next_measure_id - 1 > s_meta.max_measure_id) {
        s_meta.max_measure_id = s_next_measure_id - 1;
    }

    xSemaphoreGive(s_pending_mutex);
    return ESP_OK;
}

esp_err_t pending_store_append(const measurement_record_t *records, size_t count)
{
    if (records == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t pending_count = s_meta.tail - s_meta.head;
    size_t entry_size = sizeof(pending_entry_t);
    size_t max_entries = (512U * 1024U) / entry_size;

    if (pending_count + count > max_entries) {
        ESP_LOGE(TAG, "Pending store full (%u + %u > %u)",
                 (unsigned)pending_count, (unsigned)count, (unsigned)max_entries);
        xSemaphoreGive(s_pending_mutex);
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(PENDING_DATA_PATH, "ab");
    if (f == NULL) {
        ESP_LOGE(TAG, "Cannot open pending data for append");
        xSemaphoreGive(s_pending_mutex);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; i++) {
        pending_entry_t entry;
        entry.magic = PENDING_ENTRY_MAGIC;
        entry.record = records[i];
        entry.crc32 = compute_entry_crc(&entry);

        if (fwrite(&entry, sizeof(entry), 1, f) != 1) {
            ESP_LOGE(TAG, "Write failed at entry %u", (unsigned)i);
            fclose(f);
            xSemaphoreGive(s_pending_mutex);
            return ESP_FAIL;
        }

        if (records[i].measure_id > s_meta.max_measure_id) {
            s_meta.max_measure_id = records[i].measure_id;
        }
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    s_meta.tail += (uint32_t)count;

    size_t new_count = s_meta.tail - s_meta.head;
    size_t warn_threshold = (max_entries * PENDING_CAPACITY_WARN_PCT) / 100;
    if (new_count > warn_threshold) {
        ESP_LOGW(TAG, "Pending store at %u%% (%u/%u entries)",
                 (unsigned)((new_count * 100) / max_entries),
                 (unsigned)new_count, (unsigned)max_entries);
    }

    meta_save();
    xSemaphoreGive(s_pending_mutex);
    return ESP_OK;
}

esp_err_t pending_store_read(pending_entry_t *out, size_t max, size_t *count)
{
    if (out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *count = 0;
    size_t available = s_meta.tail - s_meta.head;
    if (available == 0) {
        xSemaphoreGive(s_pending_mutex);
        return ESP_OK;
    }

    size_t to_read = (available < max) ? available : max;

    FILE *f = fopen(PENDING_DATA_PATH, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Cannot open pending data for read");
        xSemaphoreGive(s_pending_mutex);
        return ESP_FAIL;
    }

    long offset = (long)(s_meta.head * sizeof(pending_entry_t));
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        xSemaphoreGive(s_pending_mutex);
        return ESP_FAIL;
    }

    size_t valid = 0;
    for (size_t i = 0; i < to_read; i++) {
        pending_entry_t entry;
        if (fread(&entry, sizeof(entry), 1, f) != 1) {
            break;
        }

        if (entry.magic != PENDING_ENTRY_MAGIC) {
            ESP_LOGW(TAG, "Bad magic at index %u, skipping", (unsigned)(s_meta.head + i));
            continue;
        }

        uint32_t expected_crc = compute_entry_crc(&entry);
        if (entry.crc32 != expected_crc) {
            ESP_LOGW(TAG, "Bad CRC at index %u, skipping", (unsigned)(s_meta.head + i));
            continue;
        }

        out[valid++] = entry;
    }

    fclose(f);
    *count = valid;
    xSemaphoreGive(s_pending_mutex);
    return ESP_OK;
}

esp_err_t pending_store_remove(size_t count)
{
    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t available = s_meta.tail - s_meta.head;
    if (count > available) {
        count = available;
    }

    s_meta.head += (uint32_t)count;

    /* When head catches tail, truncate file to reclaim space */
    if (s_meta.head >= s_meta.tail) {
        s_meta.head = 0;
        s_meta.tail = 0;
        remove(PENDING_DATA_PATH);
    }

    meta_save();
    xSemaphoreGive(s_pending_mutex);
    return ESP_OK;
}

size_t pending_store_count(void)
{
    if (!s_initialized || s_pending_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return 0;
    }

    size_t n = s_meta.tail - s_meta.head;
    xSemaphoreGive(s_pending_mutex);
    return n;
}
