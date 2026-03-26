#include "esp_err.h"
#include "esp_log.h"

#include "pcf2131tfy_rtc_api.h"
#include "RTC_NXP.h"

static const char *TAG = "pcf2131tfy_rtc_api";
static PCF2131_I2C s_pcf2131((uint8_t)(0xA6 >> 1));
static bool s_rtc_ready = false;

extern "C" esp_err_t pcf2131tfy_rtc_init(void)
{
    s_pcf2131.begin();

    const esp_err_t err = s_pcf2131.last_error();
    s_rtc_ready = (err == ESP_OK);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC init backend failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

extern "C" bool pcf2131tfy_rtc_is_ready(void)
{
    return s_rtc_ready;
}

extern "C" esp_err_t pcf2131tfy_rtc_get_time(time_t *out_time)
{
    if (out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const time_t now = s_pcf2131.time(out_time);
    const esp_err_t err = s_pcf2131.last_error();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (now == (time_t)(-1)) {
        ESP_LOGW(TAG, "pcf2131tfy_rtc_get_time returned invalid value");
        return ESP_FAIL;
    }

    return ESP_OK;
}
