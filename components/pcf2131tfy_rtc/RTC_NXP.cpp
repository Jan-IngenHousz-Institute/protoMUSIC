#include "RTC_NXP.h"

RTC_NXP::RTC_NXP() : last_error_(ESP_OK)
{
}

RTC_NXP::~RTC_NXP()
{
}

time_t RTC_NXP::time(time_t *tp)
{
    clear_error();

    const time_t t = rtc_time();
    if (tp && (t != (time_t)(-1))) {
        *tp = t;
    }

    return t;
}

esp_err_t RTC_NXP::last_error(void) const
{
    return last_error_;
}

uint8_t RTC_NXP::bcd2dec(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

uint8_t RTC_NXP::dec2bcd(uint8_t v)
{
    return ((v / 10) << 4) + (v % 10);
}

void RTC_NXP::clear_error(void)
{
    last_error_ = ESP_OK;
}

void RTC_NXP::set_error(esp_err_t err)
{
    if ((err != ESP_OK) && (last_error_ == ESP_OK)) {
        last_error_ = err;
    }
}
