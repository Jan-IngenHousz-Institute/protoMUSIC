#ifndef AMBYTE_BME280_H
#define AMBYTE_BME280_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BME280_I2C_ADDR_PRIMARY ((uint8_t)0x77)
#define BME280_I2C_ADDR_SECONDARY ((uint8_t)0x76)

typedef struct {
    float temperature_c;
    float humidity_percent;
    float pressure_pa;
} bme280_reading_t;

/*
 * The platform I2C bus must be initialized before calling bme280_init().
 */
esp_err_t bme280_init(uint8_t i2c_addr);
bool bme280_is_ready(void);
esp_err_t bme280_read(bme280_reading_t *out_reading);

#ifdef __cplusplus
}
#endif

#endif
