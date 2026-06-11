#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ambyte_status.h"
#include "driver/rmt.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define NEOPIXEL_GPIO GPIO_NUM_45
#define STATUS_RMT_CHANNEL RMT_CHANNEL_0

// WS2812 timing at 40 MHz RMT clock (clk_div = 2 -> 25 ns per tick)
#define T0H 16
#define T0L 34
#define T1H 32
#define T1L 18

static bool s_status_initialized = false;
static portMUX_TYPE s_status_mutex_guard = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_status_mutex_storage;
static SemaphoreHandle_t s_status_mutex = NULL;
static const rmt_item32_t s_bit0 = {{{T0H, 1, T0L, 0}}};
static const rmt_item32_t s_bit1 = {{{T1H, 1, T1L, 0}}};

static esp_err_t ambyte_status_ensure_mutex(void)
{
    if (s_status_mutex != NULL) {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_status_mutex_guard);
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutexStatic(&s_status_mutex_storage);
    }
    taskEXIT_CRITICAL(&s_status_mutex_guard);

    return (s_status_mutex != NULL) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws2812_send_grb(uint8_t g, uint8_t r, uint8_t b)
{
    rmt_item32_t items[24];
    const uint32_t value = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    for (int i = 0; i < 24; ++i) {
        items[i] = (value & (1U << (23 - i))) ? s_bit1 : s_bit0;
    }

    esp_err_t err = rmt_write_items(STATUS_RMT_CHANNEL, items, 24, true);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_wait_tx_done(STATUS_RMT_CHANNEL, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    esp_rom_delay_us(60);
    return ESP_OK;
}

esp_err_t ambyte_status_init(void)
{
    esp_err_t err = ambyte_status_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_status_initialized) {
        (void)xSemaphoreGive(s_status_mutex);
        return ESP_OK;
    }

    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(NEOPIXEL_GPIO, STATUS_RMT_CHANNEL);
    cfg.clk_div = 2;

    err = rmt_config(&cfg);
    if (err != ESP_OK) {
        (void)xSemaphoreGive(s_status_mutex);
        return err;
    }

    err = rmt_driver_install(cfg.channel, 0, 0);
    if (err != ESP_OK) {
        (void)xSemaphoreGive(s_status_mutex);
        return err;
    }

    err = ws2812_send_grb(0, 0, 0);
    if (err != ESP_OK) {
        (void)xSemaphoreGive(s_status_mutex);
        return err;
    }

    s_status_initialized = true;
    (void)xSemaphoreGive(s_status_mutex);
    return ESP_OK;
}

esp_err_t ambyte_status_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t err = ambyte_status_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_status_initialized) {
        (void)xSemaphoreGive(s_status_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    err = ws2812_send_grb(g, r, b);
    (void)xSemaphoreGive(s_status_mutex);
    return err;
}

status_set_fn ambyte_status_get_set_fn(void)
{
    return ambyte_status_set_rgb;
}

/* ── Field-status blinker ─────────────────────────────────────────────
 * See ambyte_status.h for the colour/priority contract. Battery economics:
 * the flash itself is ~3% duty, but this WS2812 draws ~1 mA even showing
 * black — slowing/dimming on battery trims the margin; a hardware LED rail
 * gate is the real saver. Faults stay loud regardless of power source. */

#define BLINK_PERIOD_EXT_MS    3000U   /* external power: commissioning cadence */
#define BLINK_PERIOD_BATT_MS  15000U   /* battery: field cadence                */
#define BLINK_FLASH_MS          100U
#define BLINK_GAP_MS            120U   /* double-flash dark gap                 */
#define BLINK_DIM_SHIFT            3   /* battery brightness = colour >> 3      */
#define BLINK_LOW_BATT_MV      3500U
#define BLINK_TASK_STACK        4096   /* bytes; the legacy RMT driver path
                                        * (rmt_write_items + wait_tx_done) needs
                                        * more than a bare-loop task would       */
#define BLINK_TASK_PRIO            2

static ambyte_blinker_config_t s_blink;
static StaticTask_t s_blink_tcb;
static StackType_t  s_blink_stack[BLINK_TASK_STACK];
static bool         s_blink_started = false;

static bool blink_probe(bool (*fn)(void), bool dflt)
{
    return (fn != NULL) ? fn() : dflt;
}

static void blink_flash(uint8_t r, uint8_t g, uint8_t b, bool dbl)
{
    (void)ambyte_status_set_rgb(r, g, b);
    vTaskDelay(pdMS_TO_TICKS(BLINK_FLASH_MS));
    (void)ambyte_status_set_rgb(0, 0, 0);
    if (dbl) {
        vTaskDelay(pdMS_TO_TICKS(BLINK_GAP_MS));
        (void)ambyte_status_set_rgb(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(BLINK_FLASH_MS));
        (void)ambyte_status_set_rgb(0, 0, 0);
    }
}

static void blink_task(void *arg)
{
    (void)arg;
    for (;;) {
        const bool sd   = blink_probe(s_blink.sd_mounted,        true);
        const bool prov = blink_probe(s_blink.provisioned,       false);
        const bool wifi = blink_probe(s_blink.wifi_connected,    false);
        const bool run  = blink_probe(s_blink.script_running,    false);
        const bool ext  = blink_probe(s_blink.on_external_power, true);
        const uint32_t mv = (s_blink.battery_mv != NULL) ? s_blink.battery_mv() : 0;

        uint8_t  r = 0, g = 0, b = 0;
        bool     dbl    = false;
        bool     dim    = !ext;
        uint32_t period = ext ? BLINK_PERIOD_EXT_MS : BLINK_PERIOD_BATT_MS;

        if (!sd) {                                  /* fault: loud always */
            r = 100;
            dim = false;
            period = BLINK_PERIOD_EXT_MS;
        } else if (!ext && mv > 0 && mv < BLINK_LOW_BATT_MV) {
            r = 100;                                /* low battery: red ×2 */
            dbl = true;
        } else if (!prov) {
            r = 100; b = 100;                       /* purple */
        } else if (run) {
            if (wifi) g = 100; else b = 100;        /* green / blue */
        } else {
            if (wifi) { r = 100; g = 100; b = 100; }/* white */
            else      { r = 100; g = 100; }         /* yellow */
        }
        if (dim) {
            r >>= BLINK_DIM_SHIFT;
            g >>= BLINK_DIM_SHIFT;
            b >>= BLINK_DIM_SHIFT;
        }

        blink_flash(r, g, b, dbl);

        /* Chunked sleep so a fresh SD fault shows within ~1 s even on the
         * slow battery cadence. */
        uint32_t slept = BLINK_FLASH_MS + (dbl ? BLINK_GAP_MS + BLINK_FLASH_MS : 0U);
        while (slept < period) {
            uint32_t step = (period - slept > 1000U) ? 1000U : (period - slept);
            vTaskDelay(pdMS_TO_TICKS(step));
            slept += step;
            if (sd && !blink_probe(s_blink.sd_mounted, true)) {
                break;                              /* new fault — show it now */
            }
        }
    }
}

esp_err_t ambyte_status_blinker_start(const ambyte_blinker_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status_initialized || s_blink_started) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(&s_blink, cfg, sizeof(s_blink));
    if (xTaskCreateStatic(blink_task, "led_blink", BLINK_TASK_STACK, NULL,
                          BLINK_TASK_PRIO, s_blink_stack, &s_blink_tcb) == NULL) {
        return ESP_FAIL;
    }
    s_blink_started = true;
    return ESP_OK;
}
