/*!
 * @brief     ADPD6000 configuration header
 * @copyright MSU-PRL Kramer Lab
 * @author      Jingcheng Huang
 * @date        20230224
 */

#ifndef __U_ADPD6000_H__
#define __U_ADPD6000_H__
 
#include <Arduino.h>
#include "lib/ADPD6000/adi_adpd6000.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "../pin_config.h"
#include "u_adpd6100_typedef.h"
class ADPD6;

class ADPD6{
    public:

    adi_adpd6000_device_t handle;
    ADPD6();
    ~ADPD6();
    bool begin();
    int32_t write_reg(uint32_t reg_addr, uint16_t *reg_data);
    int32_t read_reg(uint32_t reg_addr);
    int32_t clear_fifo();
    int32_t run_freq(uint32_t freq);
    int32_t num_ts(uint8_t);
    int32_t RUN();
    int32_t STOP();
    uint16_t fifo_count();
    int32_t readfifo(uint16_t num_samples, uint8_t width, uint32_t* data);
    bool chip_check = false;
    struct system_config sys_config;
    struct ts_led led_config;
    struct ts_SNR SNR_config;
    struct ts_DI_timing DI_config;
    struct ts_AI_timing AI_config;
    struct ts_signal signal_config;
    struct GPIO0_config gpio_config;


    int32_t ts_setup(adi_adpd6000_slot_e timeslot_no,struct ts_led *init);
    int32_t ts_setup(adi_adpd6000_slot_e timeslot_no, struct ts_SNR *init);
    int32_t ts_setup(adi_adpd6000_slot_e timeslot_no, struct ts_signal *init);
    int32_t ts_setup(adi_adpd6000_slot_e timeslot_no, struct ts_DI_timing *init);
    int32_t ts_setup(adi_adpd6000_slot_e timeslot_no, struct ts_AI_timing *init);
    
    int32_t gpio_setup(struct GPIO0_config *init);
    int32_t global_setup(struct system_config *init);


    int32_t preset_config_1(uint8_t ts, uint8_t num_integ);
    int32_t preset_config_2(uint8_t ts, uint8_t num_integ);
    int32_t preset_config_2x(uint8_t ts, uint8_t num_integ, uint8_t lit_offset, uint8_t dark1_offset, uint8_t dark2_offset, uint8_t pulse_offset, uint8_t pulse_duration);
    int32_t preset_config_3(uint8_t ts, uint8_t num_integ);
    int32_t preset_config_4(uint8_t ts);
    int32_t repeats_only(uint8_t ts, uint16_t num_integration, uint8_t num_repeats);
    int32_t preset_config_ext_fast(uint8_t ts);
    int32_t preset_config_ext_fast(uint8_t ts, uint8_t);




    // int32_t current_sweep(uint8_t led, uint8_t PD_IN, uint8_t gain, uint32_t *data);
    // int32_t augogain(uint8_t led, uint8_t PD_IN, uint8_t mode, struct light_gain_setting * output);
    // int32_t augogain(uint8_t led, uint8_t PD_IN, uint8_t mode);
    // int32_t led_input_preset(struct light_gain_setting * output);
    // int32_t DI_FluoRef(uint8_t ts, uint8_t num_integ);
    // int32_t DI_ambient(uint8_t ts, uint8_t num_integ);
    // int32_t current_sweep2(uint8_t led, uint8_t PD_IN, uint8_t gain, uint8_t ac, uint32_t *data);


    




    private:
    bool _spi_device_attached = false;
    spi_device_interface_config_t adpd_spi_dev;
    spi_device_handle_t adpd1_spi_handle = NULL;


    void load_default(struct ts_led *ts);
    void load_default(struct ts_SNR *ts);
    void load_default(struct ts_signal *ts);
    void load_default(struct ts_DI_timing *ts);
    void load_default(struct ts_AI_timing *ts);
    void load_default(struct system_config *ts);
    void load_default(struct GPIO0_config *ts);

};

#endif