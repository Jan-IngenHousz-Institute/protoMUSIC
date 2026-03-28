#ifndef AMBYTE_SENSING_PORT_H
#define AMBYTE_SENSING_PORT_H

#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_percent;
    float pressure_pa;
} measurement_t;

typedef esp_err_t (*sensor_read_fn)(measurement_t *out);
typedef esp_err_t (*clock_read_fn)(time_t *out);

#ifdef __cplusplus
}
#endif

#endif
