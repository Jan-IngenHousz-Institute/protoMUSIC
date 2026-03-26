#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf2131tfy_rtc_init(void);

bool pcf2131tfy_rtc_is_ready(void);

esp_err_t pcf2131tfy_rtc_get_time(time_t *out_time);

#ifdef __cplusplus
}
#endif
