#include "i2c_bus.h"

#include <stdbool.h>

#include "freertos/semphr.h"

static StaticSemaphore_t s_i2c_bus_mutex_storage;
static SemaphoreHandle_t s_i2c_bus_mutex = NULL;
static i2c_bus_config_t s_i2c_bus_config = {0};
static bool s_i2c_bus_initialized = false;

static SemaphoreHandle_t i2c_bus_ensure_mutex(void)
{
    if (s_i2c_bus_mutex == NULL) {
        s_i2c_bus_mutex = xSemaphoreCreateMutexStatic(&s_i2c_bus_mutex_storage);
    }

    return s_i2c_bus_mutex;
}

static bool i2c_bus_config_matches(const i2c_bus_config_t *lhs, const i2c_bus_config_t *rhs)
{
    return (lhs->port == rhs->port) && (lhs->sda_gpio == rhs->sda_gpio) &&
           (lhs->scl_gpio == rhs->scl_gpio) && (lhs->clock_speed_hz == rhs->clock_speed_hz);
}

esp_err_t i2c_bus_init(const i2c_bus_config_t *config)
{
    if ((config == NULL) || (config->clock_speed_hz == 0) ||
        (config->sda_gpio == GPIO_NUM_NC) || (config->scl_gpio == GPIO_NUM_NC)) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = i2c_bus_ensure_mutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_i2c_bus_initialized) {
        const esp_err_t status = i2c_bus_config_matches(config, &s_i2c_bus_config)
                                     ? ESP_OK
                                     : ESP_ERR_INVALID_STATE;
        xSemaphoreGive(mutex);
        return status;
    }

    const i2c_config_t idf_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->clock_speed_hz,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(config->port, &idf_config);
    if (err == ESP_OK) {
        err = i2c_driver_install(config->port, I2C_MODE_MASTER, 0, 0, 0);
    }

    if (err == ESP_OK) {
        s_i2c_bus_config = *config;
        s_i2c_bus_initialized = true;
    }

    xSemaphoreGive(mutex);
    return err;
}

esp_err_t i2c_bus_lock(TickType_t timeout_ticks)
{
    if (!s_i2c_bus_initialized || (s_i2c_bus_mutex == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    return (xSemaphoreTake(s_i2c_bus_mutex, timeout_ticks) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t i2c_bus_unlock(void)
{
    if (!s_i2c_bus_initialized || (s_i2c_bus_mutex == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    return (xSemaphoreGive(s_i2c_bus_mutex) == pdTRUE) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t i2c_bus_get_config(i2c_bus_config_t *out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2c_bus_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_config = s_i2c_bus_config;
    return ESP_OK;
}

esp_err_t i2c_bus_get_port(i2c_port_t *out_port)
{
    if (out_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2c_bus_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_port = s_i2c_bus_config.port;
    return ESP_OK;
}
