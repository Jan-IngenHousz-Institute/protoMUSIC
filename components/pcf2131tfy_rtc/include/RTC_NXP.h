#ifndef AMBYTE_RTC_NXP_H
#define AMBYTE_RTC_NXP_H

#include <stdint.h>
#include <time.h>

#include "esp_err.h"

class RTC_NXP
{
public:
    enum alarm_setting {
        SECOND,
        MINUTE,
        HOUR,
        DAY,
        WEEKDAY,
    };

    RTC_NXP();
    virtual ~RTC_NXP();

    virtual void begin(void) = 0;
    time_t time(time_t *tp);

    virtual void set(struct tm *now_tm) = 0;
    virtual bool oscillator_stop(void) = 0;
    virtual void alarm(alarm_setting digit, int val) = 0;
    virtual void alarm_clear(void) = 0;
    virtual void alarm_disable(void) = 0;
    virtual uint8_t int_clear(void) = 0;

    esp_err_t last_error(void) const;

protected:
    virtual time_t rtc_time(void) = 0;

    static uint8_t bcd2dec(uint8_t v);
    static uint8_t dec2bcd(uint8_t v);

    void clear_error(void);
    void set_error(esp_err_t err);

private:
    esp_err_t last_error_;
};

class PCF2131_base : public RTC_NXP
{
public:
    enum reg_num {
        Control_1,
        Control_2,
        Control_3,
        Control_4,
        Control_5,
        SR_Reset,
        _100th_Seconds,
        Seconds,
        Minutes,
        Hours,
        Days,
        Weekdays,
        Months,
        Years,
        Second_alarm,
        Minute_alarm,
        Hour_alarm,
        Day_alarm,
        Weekday_alarm,
        CLKOUT_ctl,
        Timestp_ctl1,
        Sec_timestp1,
        Min_timestp1,
        Hour_timestp1,
        Day_timestp1,
        Mon_timestp1,
        Year_timestp1,
        Timestp_ctl2,
        Sec_timestp2,
        Min_timestp2,
        Hour_timestp2,
        Day_timestp2,
        Mon_timestp2,
        Year_timestp2,
        Timestp_ctl3,
        Sec_timestp3,
        Min_timestp3,
        Hour_timestp3,
        Day_timestp3,
        Mon_timestp3,
        Year_timestp3,
        Timestp_ctl4,
        Sec_timestp4,
        Min_timestp4,
        Hour_timestp4,
        Day_timestp4,
        Mon_timestp4,
        Year_timestp4,
        Aging_offset,
        INT_A_MASK1,
        INT_A_MASK2,
        INT_B_MASK1,
        INT_B_MASK2,
        Watchdg_tim_ctl,
        Watchdg_tim_val,
    };

    enum periodic_int_select {
        DISABLE,
        EVERY_SECOND,
        EVERY_MINUTE,
    };

    enum timestamp_setting {
        LAST,
        FIRST,
    };

    enum clock_out_frequency {
        FREQ_32768_HZ,
        FREQ_16384_HZ,
        FREQ_8192_HZ,
        FREQ_4096_HZ,
        FREQ_2048_HZ,
        FREQ_1024_HZ,
        FREQ_1_HZ,
        FREQ_DISABLE,
    };

    PCF2131_base();
    virtual ~PCF2131_base();

    void begin(void) override;
    bool oscillator_stop(void) override;
    time_t rtc_time(void) override;
    void set(struct tm *now_tm) override;

    void alarm(alarm_setting digit, int val) override;
    void alarm(alarm_setting digit, int val, int int_sel);

    void alarm_clear(void) override;
    void alarm_disable(void) override;

    void timestamp(int num, timestamp_setting ts_setting, int int_sel = 0);
    time_t timestamp(int num);

    uint8_t int_clear(void) override;
    uint8_t int_clear(uint8_t *state_p);

    void periodic_interrupt_enable(periodic_int_select sel, int int_sel = 0);

protected:
    virtual void _reg_w(uint8_t reg, const uint8_t *vp, int len) = 0;
    virtual void _reg_r(uint8_t reg, uint8_t *vp, int len) = 0;
    virtual void _reg_w(uint8_t reg, uint8_t val) = 0;
    virtual uint8_t _reg_r(uint8_t reg) = 0;
    virtual void _bit_op8(uint8_t reg, uint8_t mask, uint8_t val) = 0;

private:
    const int int_mask_reg[2][2] = {
        {INT_A_MASK1, INT_A_MASK2},
        {INT_B_MASK1, INT_B_MASK2},
    };
};

class PCF2131_I2C : public PCF2131_base
{
public:
    explicit PCF2131_I2C(uint8_t i2c_address = (0xA6 >> 1));
    virtual ~PCF2131_I2C();

private:
    void _reg_w(uint8_t reg, const uint8_t *vp, int len) override;
    void _reg_r(uint8_t reg, uint8_t *vp, int len) override;
    void _reg_w(uint8_t reg, uint8_t val) override;
    uint8_t _reg_r(uint8_t reg) override;
    void _bit_op8(uint8_t reg, uint8_t mask, uint8_t val) override;

    uint8_t i2c_address_;
};

#endif
