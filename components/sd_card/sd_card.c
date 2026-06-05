#include "sd_card.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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

static esp_err_t sdcard_mount_locked(bool quiet)
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
        if (!quiet) {
            ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        }
        s_sdcard.card = NULL;
        return err;
    }

    s_sdcard.mounted = true;
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

    err = sdcard_mount_locked(false);
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

/* ── Hot-plug monitor ─────────────────────────────────────────────────── */

static TaskHandle_t      s_monitor_task = NULL;
static sdcard_state_cb_t s_state_cb     = NULL;
static uint32_t          s_monitor_period_ms = 2000;   /* removal-detection probe (card in) */

/* While the card is OUT, only retry remounting this often. Probing a missing
 * card more frequently gains nothing and just burns a failed init cycle. */
#define SD_MONITOR_OUT_RETRY_MS  (5 * 60 * 1000)        /* 5 minutes */

/* One probe step: try to detect a state transition. Returns the new mounted
 * state (or current state if no change). Takes the lock briefly. */
static bool sdcard_probe_step(void)
{
    if (sdcard_lock() != ESP_OK) {
        return s_sdcard.mounted;     /* lock busy — report last known */
    }

    bool was_mounted = s_sdcard.mounted;

    if (was_mounted && s_sdcard.card != NULL) {
        /* CMD13 — fast card-status query. Fails if the card is gone. */
        esp_err_t err = sdmmc_get_status(s_sdcard.card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "card lost (sdmmc_get_status: %s) — unmounting",
                     esp_err_to_name(err));
            esp_err_t u = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_sdcard.card);
            if (u != ESP_OK) {
                ESP_LOGW(TAG, "unmount after card-loss: %s", esp_err_to_name(u));
            }
            s_sdcard.card    = NULL;
            s_sdcard.mounted = false;
        }
    } else if (!was_mounted) {
        /* Card was out — try to remount. Quiet on failure (likely no card). */
        esp_err_t err = sdcard_mount_locked(true);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "card detected — remounted");
        }
    }

    bool now_mounted = s_sdcard.mounted;
    sdcard_unlock();
    return (now_mounted != was_mounted) ? now_mounted : was_mounted;
    /* Caller compares against its own previous state to fire transitions. */
}

static void sdcard_monitor_task(void *arg)
{
    (void)arg;
    bool last = sdcard_is_mounted();

    /* Stagger the first probe so we don't fight boot-time SD activity. */
    vTaskDelay(pdMS_TO_TICKS(s_monitor_period_ms));

    while (1) {
        bool now = sdcard_probe_step();
        if (now != last) {
            ESP_LOGI(TAG, "state transition: %s -> %s",
                     last ? "mounted" : "out",
                     now  ? "mounted" : "out");
            if (s_state_cb != NULL) {
                s_state_cb(now);
            }
            last = now;
        }
        /* Card in → probe quickly so a pulled card is caught (and the script
         * stopped) within seconds. Card out → retry remount every few minutes. */
        vTaskDelay(pdMS_TO_TICKS(now ? s_monitor_period_ms : SD_MONITOR_OUT_RETRY_MS));
    }
}

esp_err_t sdcard_start_monitor(uint32_t period_ms, sdcard_state_cb_t cb)
{
    if (s_monitor_task != NULL) {
        return ESP_OK;     /* already running */
    }
    if (period_ms < 200) period_ms = 200;
    s_monitor_period_ms = period_ms;
    s_state_cb          = cb;

    /* Fully silence the ESP-IDF SDMMC layers — they log the failed card-init at
     * ERROR level on every remount retry while the card is out, which WARN does
     * not suppress. The sd_card component reports mount state through its own
     * logs, so we lose no useful visibility. */
    esp_log_level_set("sdmmc_common",   ESP_LOG_NONE);
    esp_log_level_set("vfs_fat_sdmmc",  ESP_LOG_NONE);

    /* 12 KB stack: the remount path goes through esp_vfs_fat_sdmmc_mount +
     * FATFS, and the cb fans out to sqlite_persistence_on_sd_restored() which
     * reopens SQLite (heavy stack user). 4 KB overflowed; 8 KB was marginal. */
    BaseType_t ok = xTaskCreate(sdcard_monitor_task, "sd_monitor",
                                12288, NULL, 2, &s_monitor_task);
    if (ok != pdPASS) {
        s_monitor_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "hot-plug monitor started (period=%u ms)", (unsigned)period_ms);
    return ESP_OK;
}
