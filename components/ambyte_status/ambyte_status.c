#include <stdbool.h>
#include <stdint.h>

#include "ambyte_status.h"
#include "driver/rmt.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
