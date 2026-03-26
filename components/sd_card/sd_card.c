#include "sd_card.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#define TAG "sd_card"
#define SD_CARD_ACCESS_TIMEOUT_TICKS pdMS_TO_TICKS(1000)

typedef struct {
    gpio_num_t clk;
    gpio_num_t cmd;
    gpio_num_t d0;
    gpio_num_t d1;
    gpio_num_t d2;
    gpio_num_t d3;
    int width;
} sdcard_pin_config_t;

typedef struct {
    SemaphoreHandle_t mutex;
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    sdmmc_card_t *card;
    bool initialized;
    bool mounted;
} sdcard_service_t;

typedef enum {
    SDCARD_NEWLINE_NONE = 0,
    SDCARD_NEWLINE_IF_MISSING,
    SDCARD_NEWLINE_ALWAYS,
} sdcard_newline_mode_t;

static portMUX_TYPE s_sdcard_mutex_guard = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_sdcard_mutex_storage;

static const sdcard_pin_config_t s_sdcard_pins = {
    .clk = GPIO_NUM_11,
    .cmd = GPIO_NUM_12,
    .d0 = GPIO_NUM_10,
    .d1 = GPIO_NUM_9,
    .d2 = GPIO_NUM_13,
    .d3 = GPIO_NUM_14,
    .width = 4,
};

static const esp_vfs_fat_sdmmc_mount_config_t s_mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
};

static sdcard_service_t s_sdcard = {
    .mutex = NULL,
    .host = {0},
    .slot = {0},
    .card = NULL,
    .initialized = false,
    .mounted = false,
};

static esp_err_t sdcard_ensure_mutex(void)
{
    if (s_sdcard.mutex != NULL) {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_sdcard_mutex_guard);
    if (s_sdcard.mutex == NULL) {
        s_sdcard.mutex = xSemaphoreCreateMutexStatic(&s_sdcard_mutex_storage);
    }
    taskEXIT_CRITICAL(&s_sdcard_mutex_guard);

    if (s_sdcard.mutex == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t sdcard_lock(void)
{
    const esp_err_t err = sdcard_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_sdcard.mutex, SD_CARD_ACCESS_TIMEOUT_TICKS) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void sdcard_unlock(void)
{
    if (s_sdcard.mutex != NULL) {
        (void)xSemaphoreGive(s_sdcard.mutex);
    }
}

static void sdcard_fill_default_host_slot(void)
{
    s_sdcard.host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    s_sdcard.host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    s_sdcard.slot = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    s_sdcard.slot.clk = s_sdcard_pins.clk;
    s_sdcard.slot.cmd = s_sdcard_pins.cmd;
    s_sdcard.slot.d0 = s_sdcard_pins.d0;
    s_sdcard.slot.d1 = s_sdcard_pins.d1;
    s_sdcard.slot.d2 = s_sdcard_pins.d2;
    s_sdcard.slot.d3 = s_sdcard_pins.d3;
    s_sdcard.slot.width = s_sdcard_pins.width;
}

static esp_err_t sdcard_init_locked(void)
{
    if (s_sdcard.initialized) {
        return ESP_OK;
    }

    sdcard_fill_default_host_slot();
    s_sdcard.initialized = true;
    return ESP_OK;
}

static esp_err_t sdcard_mount_locked(void)
{
    if (s_sdcard.mounted) {
        return ESP_OK;
    }

    esp_err_t err = sdcard_init_locked();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT,
        &s_sdcard.host,
        &s_sdcard.slot,
        &s_mount_config,
        &s_sdcard.card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_sdcard.card = NULL;
        return err;
    }

    s_sdcard.mounted = true;
    return ESP_OK;
}

static bool path_segment_is_special(const char *segment, size_t len)
{
    return ((len == 1) && (segment[0] == '.')) ||
           ((len == 2) && (segment[0] == '.') && (segment[1] == '.'));
}

// Reject traversal, empty segments, backslashes, and control characters.
static bool path_suffix_is_valid(const char *suffix)
{
    if ((suffix == NULL) || (*suffix == '\0')) {
        return false;
    }

    const char *segment = suffix;
    while (*segment != '\0') {
        if (*segment == '/') {
            return false;
        }

        const char *next = strchr(segment, '/');
        const size_t len = (next != NULL) ? (size_t)(next - segment) : strlen(segment);
        if ((len == 0) || path_segment_is_special(segment, len)) {
            return false;
        }

        for (size_t i = 0; i < len; ++i) {
            const unsigned char ch = (unsigned char)segment[i];
            if ((ch < 0x20U) || (segment[i] == '\\')) {
                return false;
            }
        }

        if (next == NULL) {
            return true;
        }

        segment = next + 1;
        if (*segment == '\0') {
            return false;
        }
    }

    return false;
}

static esp_err_t resolve_path(const char *name, char *out, size_t out_len)
{
    if ((name == NULL) || (out == NULL) || (out_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    int written = 0;
    if (name[0] == '/') {
        const size_t mount_len = strlen(SD_MOUNT_POINT);
        if (strncmp(name, SD_MOUNT_POINT, mount_len) != 0) {
            return ESP_ERR_INVALID_ARG;
        }

        const char suffix_lead = name[mount_len];
        if (suffix_lead != '/') {
            return ESP_ERR_INVALID_ARG;
        }

        if (!path_suffix_is_valid(name + mount_len + 1)) {
            return ESP_ERR_INVALID_ARG;
        }

        written = snprintf(out, out_len, "%s", name);
    } else {
        if (!path_suffix_is_valid(name)) {
            return ESP_ERR_INVALID_ARG;
        }

        written = snprintf(out, out_len, "%s/%s", SD_MOUNT_POINT, name);
    }

    if ((written < 0) || (written >= (int)out_len)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t sdcard_write_payload_locked(
    const char *name,
    const char *payload,
    const char *mode,
    sdcard_newline_mode_t newline_mode)
{
    const char *safe_payload = (payload != NULL) ? payload : "";

    esp_err_t err = sdcard_mount_locked();
    if (err != ESP_OK) {
        return err;
    }

    char path[SD_CARD_PATH_MAX];
    err = resolve_path(name, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }

    FILE *fp = fopen(path, mode);
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed opening %s: errno=%d", path, errno);
        return ESP_FAIL;
    }

    const size_t payload_len = strlen(safe_payload);
    const size_t written = fwrite(safe_payload, 1, payload_len, fp);
    bool write_newline = false;

    if (newline_mode == SDCARD_NEWLINE_ALWAYS) {
        write_newline = true;
    } else if ((newline_mode == SDCARD_NEWLINE_IF_MISSING) &&
               ((payload_len == 0) || (safe_payload[payload_len - 1] != '\n'))) {
        write_newline = true;
    }

    if (write_newline) {
        if (fputc('\n', fp) == EOF) {
            fclose(fp);
            return ESP_FAIL;
        }
    }

    const int flush_rc = fflush(fp);
    const int close_rc = fclose(fp);
    if ((written != payload_len) || (flush_rc != 0) || (close_rc != 0)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t sdcard_touch_file_locked(const char *name, char *out_path, size_t out_path_len)
{
    if ((out_path == NULL) || (out_path_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sdcard_mount_locked();
    if (err != ESP_OK) {
        return err;
    }

    err = resolve_path(name, out_path, out_path_len);
    if (err != ESP_OK) {
        return err;
    }

    FILE *fp = fopen(out_path, "a");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed opening %s: errno=%d", out_path, errno);
        return ESP_FAIL;
    }

    const int flush_rc = fflush(fp);
    const int close_rc = fclose(fp);
    if ((flush_rc != 0) || (close_rc != 0)) {
        ESP_LOGE(TAG, "Failed finalizing %s: errno=%d", out_path, errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t sdcard_init_default(void)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_init_locked();
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_mount(void)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_mount_locked();
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_unmount(void)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    if (s_sdcard.mounted) {
        err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_sdcard.card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SD unmount failed: %s", esp_err_to_name(err));
            sdcard_unlock();
            return err;
        }

        s_sdcard.card = NULL;
        s_sdcard.mounted = false;
    }

    sdcard_unlock();
    return ESP_OK;
}

bool sdcard_is_mounted(void)
{
    bool mounted = false;

    if (sdcard_lock() != ESP_OK) {
        return false;
    }

    mounted = s_sdcard.mounted;
    sdcard_unlock();
    return mounted;
}

esp_err_t sdcard_write_file(const char *name, const char *data)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_write_payload_locked(name, data, "w", SDCARD_NEWLINE_NONE);
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_append_file(const char *name, const char *data)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_write_payload_locked(name, data, "a", SDCARD_NEWLINE_NONE);
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_write_line(const char *name, const char *line)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_write_payload_locked(name, line, "w", SDCARD_NEWLINE_IF_MISSING);
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_append_line(const char *name, const char *line)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_write_payload_locked(name, line, "a", SDCARD_NEWLINE_IF_MISSING);
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_append_line_exact(const char *name, const char *line)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_write_payload_locked(name, line, "a", SDCARD_NEWLINE_ALWAYS);
    sdcard_unlock();
    return err;
}

esp_err_t sdcard_read_line(const char *name, char *out, size_t out_len)
{
    return sdcard_read_line_at(name, 0, out, out_len);
}

esp_err_t sdcard_read_line_at(const char *name, unsigned int line_index, char *out, size_t out_len)
{
    if ((name == NULL) || (out == NULL) || (out_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_mount_locked();
    if (err != ESP_OK) {
        sdcard_unlock();
        return err;
    }

    char path[SD_CARD_PATH_MAX];
    err = resolve_path(name, path, sizeof(path));
    if (err != ESP_OK) {
        sdcard_unlock();
        return err;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        const int saved_errno = errno;
        sdcard_unlock();
        if (saved_errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }

        ESP_LOGE(TAG, "Failed opening %s: errno=%d", path, saved_errno);
        return ESP_FAIL;
    }

    memset(out, 0, out_len);
    for (unsigned int idx = 0; idx <= line_index; ++idx) {
        if (fgets(out, (int)out_len, fp) == NULL) {
            fclose(fp);
            sdcard_unlock();
            return ESP_ERR_NOT_FOUND;
        }
        if (idx < line_index) {
            out[0] = '\0';
        }
    }

    const size_t len = strlen(out);
    if ((len > 0) && (out[len - 1] == '\n')) {
        out[len - 1] = '\0';
    }
    const size_t trimmed_len = strlen(out);
    if ((trimmed_len > 0) && (out[trimmed_len - 1] == '\r')) {
        out[trimmed_len - 1] = '\0';
    }

    fclose(fp);
    sdcard_unlock();
    return ESP_OK;
}

esp_err_t sdcard_file_exists(const char *name, bool *out_exists)
{
    if ((name == NULL) || (out_exists == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_exists = false;

    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_mount_locked();
    if (err != ESP_OK) {
        sdcard_unlock();
        return err;
    }

    char path[SD_CARD_PATH_MAX];
    err = resolve_path(name, path, sizeof(path));
    if (err != ESP_OK) {
        sdcard_unlock();
        return err;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        *out_exists = true;
        sdcard_unlock();
        return ESP_OK;
    }

    if (errno != ENOENT) {
        ESP_LOGE(TAG, "Failed stating %s: errno=%d", path, errno);
        sdcard_unlock();
        return ESP_FAIL;
    }

    sdcard_unlock();
    return ESP_OK;
}

esp_err_t sdcard_ensure_file(const char *name, char *out_path, size_t out_path_len)
{
    esp_err_t err = sdcard_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = sdcard_touch_file_locked(name, out_path, out_path_len);
    sdcard_unlock();
    return err;
}
