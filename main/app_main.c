#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "CLI.h"
#include "ambyte_status.h"
#include "bme280.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "lua_runner.h"
#include "nvs_flash.h"
#include "pcf2131tfy_rtc_api.h"
#include "wifi_manager.h"

#define APP_TAG "APP_MAIN"

static const i2c_bus_config_t s_i2c_bus_cfg = {
    .port = I2C_BUS_DEFAULT_PORT,
    .sda_gpio = I2C_BUS_DEFAULT_SDA_GPIO,
    .scl_gpio = I2C_BUS_DEFAULT_SCL_GPIO,
    .clock_speed_hz = I2C_BUS_DEFAULT_SPEED_HZ,
};

static esp_err_t app_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }

    return err;
}

static esp_err_t app_start_wifi(void)
{
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "Wi-Fi manager initialized");

    err = wifi_manager_start();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "Wi-Fi station started");

    err = wifi_manager_connect_configured();
    if (err == ESP_OK) {
        ESP_LOGI(APP_TAG, "Wi-Fi connected");
    } else if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(APP_TAG, "Wi-Fi credentials not provisioned; skipping configured connect");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(APP_TAG, "Wi-Fi initial connect timed out; reconnect continues in background");
    } else {
        ESP_LOGW(APP_TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static esp_err_t app_init_i2c_and_sensors(void)
{
    esp_err_t err = i2c_bus_init(&s_i2c_bus_cfg);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(APP_TAG, "I2C bus initialized");

    err = pcf2131tfy_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "RTC init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "RTC initialized");
    }

    if (pcf2131tfy_rtc_is_ready()) {
        time_t rtc_now = 0;
        if (pcf2131tfy_rtc_get_time(&rtc_now) == ESP_OK) {
            ESP_LOGI(APP_TAG, "RTC time: %lld", (long long)rtc_now);
        } else {
            ESP_LOGW(APP_TAG, "RTC time read failed");
        }
    }

    err = bme280_init(BME280_I2C_ADDR_SECONDARY);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "BME280 init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "BME280 initialized");
    }

    return ESP_OK;
}

static void app_start_lua_runner(void)
{
    const esp_err_t err = lua_runner_start();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "Lua runner failed to start: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(APP_TAG, "Lua runner started");
}

static void app_start_cli(void)
{
    const esp_err_t err = cli_start();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "CLI start failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(APP_TAG, "CLI started");
}

void app_main(void)
{
    ESP_LOGI(APP_TAG, "app_main entered");

    esp_err_t err = app_init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "NVS initialized successfully");

    err = app_start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
        return;
    }

    err = app_init_i2c_and_sensors();
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "I2C startup failed: %s", esp_err_to_name(err));
        return;
    }

    err = ambyte_status_init();
    if (err != ESP_OK) {
        ESP_LOGW(APP_TAG, "Status LED init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(APP_TAG, "Status LED initialized");
    }

    app_start_lua_runner();
    app_start_cli();

    ESP_LOGI(APP_TAG, "Startup sequence complete");
}
