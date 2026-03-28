#ifndef AMBYTE_DEVICE_STATUS_PORT_H
#define AMBYTE_DEVICE_STATUS_PORT_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*status_set_fn)(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif
