#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "device_status_port.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ambyte_status_init(void);
esp_err_t ambyte_status_set_rgb(uint8_t r, uint8_t g, uint8_t b);
status_set_fn ambyte_status_get_set_fn(void);

#ifdef __cplusplus
}
#endif
