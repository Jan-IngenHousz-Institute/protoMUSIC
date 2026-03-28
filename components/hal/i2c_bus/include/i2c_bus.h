#ifndef AMBYTE_I2C_BUS_H
#define AMBYTE_I2C_BUS_H

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_BUS_DEFAULT_PORT I2C_NUM_0
#define I2C_BUS_DEFAULT_SDA_GPIO GPIO_NUM_39
#define I2C_BUS_DEFAULT_SCL_GPIO GPIO_NUM_38
#define I2C_BUS_DEFAULT_SPEED_HZ 100000U

typedef struct {
    i2c_port_t port;
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t clock_speed_hz;
} i2c_bus_config_t;

esp_err_t i2c_bus_init(const i2c_bus_config_t *config);
esp_err_t i2c_bus_lock(TickType_t timeout_ticks);
esp_err_t i2c_bus_unlock(void);
esp_err_t i2c_bus_get_config(i2c_bus_config_t *out_config);
esp_err_t i2c_bus_get_port(i2c_port_t *out_port);
esp_err_t i2c_bus_check_and_recover(i2c_port_t port);
esp_err_t i2c_bus_probe_addr(i2c_port_t port, uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif
