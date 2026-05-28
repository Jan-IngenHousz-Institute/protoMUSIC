/*!
 * @brief     PPG APIs Implementation
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */

/*!
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

/*============= I N C L U D E S ============*/
#include "adi_adpd6000.h"
#include "math.h"
   
/*============= D E F I N E S ==============*/

/*============= D A T A ====================*/
uint16_t ppg_sample_count;
adi_adpd6000_ppg_agc_run_t ppg_agc_run[12];
   
/*============= C O D E ====================*/
int32_t adi_adpd6000_ppg_set_slot_mode(adi_adpd6000_device_t *device, adi_adpd6000_ppg_slot_mode_e mode)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(mode > API_ADPD6000_PPG_SLOT_ABCDEFGHIJKL);
    
    err = adi_adpd6000_hal_bf_write(device, BF_PPG_TIMESLOT_EN_INFO, mode);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_get_slot_num(adi_adpd6000_device_t *device, uint8_t *slot_num)
{
    int32_t  err;
    uint16_t data;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    
    err = adi_adpd6000_hal_bf_read(device, BF_PPG_TIMESLOT_EN_INFO, &data);
    ADPD6000_ERROR_RETURN(err);
    *slot_num = data;
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_get_channel_num(adi_adpd6000_device_t *device, uint8_t *channel_num)
{
    int32_t  err;
    uint8_t i, slot_num, channel;
    uint16_t chl2_en;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    
    err = adi_adpd6000_ppg_get_slot_num(device, &slot_num);
    ADPD6000_ERROR_RETURN(err);
    
    channel = 0;
    for (i = 0; i < slot_num; i++)
    {
         err = adi_adpd6000_hal_bf_read(device, ADPD6000_TIME_SLOT_SPAN * i + BF_CH2_EN_A_INFO, &chl2_en);
         ADPD6000_ERROR_RETURN(err);
         channel = (chl2_en == 0) ? channel + 1 : channel + 2;
    }
    *channel_num = channel;
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_tia_set_input_res(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_input_res_e res)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(res > API_ADPD6000_PPG_TIA_INPUT_RES_6K5);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_INPUT_R_SELECT_A_INFO, res);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_tia_set_gain_res(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_tia_gain_res_e gain)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(channel > 1);
    ADPD6000_INVALID_PARAM_RETURN(gain > API_ADPD6000_PPG_TIA_GAIN_RES_12K5);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_TIA_GAIN_CH1_A_INFO + channel * 3, gain);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_tia_set_vref_value(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_vref_value_e vref_value)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(vref_value > API_ADPD6000_PPG_TIA_VREF_1P265);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_AFE_TRIM_VREF_A_INFO, vref_value);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_tia_set_vref_pulse_alt_value(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_vref_value_e alt_value)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(alt_value > API_ADPD6000_PPG_TIA_VREF_1P265);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_VREF_PULSE_VAL_A_INFO, alt_value);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_tia_enable_vref_pulse(adi_adpd6000_device_t *device, uint8_t slot, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_VREF_PULSE_A_INFO, enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_enable_input_diff_mode(adi_adpd6000_device_t *device, uint8_t pair_index, bool diff_en)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(pair_index > 1);
    
    err = adi_adpd6000_hal_bf_write(device, BF_PAIR12_INFO + pair_index, diff_en ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_input_mux(adi_adpd6000_device_t *device, uint8_t slot, uint8_t pair_index, adi_adpd6000_ppg_input_mux_e mux)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(pair_index > 1);
    ADPD6000_INVALID_PARAM_RETURN(mux > API_ADPD6000_PPG_INPUT_A2_B2);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_INP12_A_INFO + pair_index * 4, mux);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_sleep_input_mux(adi_adpd6000_device_t *device, uint8_t pair_index, adi_adpd6000_ppg_sleep_input_mux_e mux)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(pair_index > 1);
    ADPD6000_INVALID_PARAM_RETURN(mux > API_ADPD6000_PPG_INPUT_SLEEP_ODD_FLOAT_EVEN_CATH2);
    
    err = adi_adpd6000_hal_bf_write(device, BF_INP_SLEEP_12_INFO + pair_index * 4, mux);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_cathode(adi_adpd6000_device_t *device, adi_adpd6000_ppg_cath_type_e cath1, adi_adpd6000_ppg_cath_type_e cath2)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(cath1 > API_ADPD6000_PPG_CATH_FLOAT);
    ADPD6000_INVALID_PARAM_RETURN(cath2 > API_ADPD6000_PPG_CATH_FLOAT);
    
    err = adi_adpd6000_hal_bf_write(device, BF_VC1_SLEEP_INFO, cath1);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_VC2_SLEEP_INFO, cath2);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_cfg_modulator(adi_adpd6000_device_t *device, uint8_t slot, 
    adi_adpd6000_ppg_mod_type_e type,  uint8_t width, uint8_t offset)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(type > API_ADPD6000_PPG_MOD_TYPE_NONFLOAT);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_MOD_TYPE_A_INFO, type);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_MOD_WIDTH_A_INFO, width);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_MOD_OFFSET_A_INFO, offset);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_enable_amp(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(channel > 1);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_CH1_AMP_DISABLE_A_INFO + channel * 4, enable ? 0 : 1);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_integ_set_gain(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_integ_gain_res_e gain)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(channel > 1);
    ADPD6000_INVALID_PARAM_RETURN(gain > API_ADPD6000_PPG_INTEG_50K_GAIN_2);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_AFE_TRIM_INT1_A_INFO + channel * 2, gain);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_integ_select_cap(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_integ_cap_e cap)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(channel > 1);
    ADPD6000_INVALID_PARAM_RETURN(cap > API_ADPD6000_PPG_INTEG_CAP_12P6);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_AFE_TRIM_INT1_CAP_A_INFO + channel, cap);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_integ_set_width(adi_adpd6000_device_t *device, uint8_t slot, uint8_t width)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_INTEG_WIDTH_A_INFO, width);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_integ_set_offset(adi_adpd6000_device_t *device, uint8_t slot, uint16_t offset)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_reg_write(device, ADPD6000_TIME_SLOT_SPAN * slot + REG_INTEG_OFFSET_A_ADDR, offset);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_integ_enable_single_clk(adi_adpd6000_device_t *device, uint8_t slot, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_SINGLE_INTEG_A_INFO, enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_current(adi_adpd6000_device_t *device, uint8_t slot, uint8_t led_idx, uint8_t current)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_CURRENT1_A_INFO + 8 * led_idx, current);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_channel(adi_adpd6000_device_t *device, uint8_t slot, uint8_t led_idx, adi_adpd6000_ppg_led_channel_e chl)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_DRIVESIDE1_A_INFO + 8 * led_idx, chl);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_mode(adi_adpd6000_device_t *device, uint8_t slot,
    uint8_t led_idx, adi_adpd6000_ppg_led_mode_e mode)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_MODE1_A_INFO + led_idx, mode);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_width(adi_adpd6000_device_t *device, uint8_t slot, uint8_t width)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_WIDTH_A_INFO, width);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_offset(adi_adpd6000_device_t *device, uint8_t slot, uint8_t offset, uint8_t sec_offset)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_OFFSET_A_INFO, offset);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LED_SECOND_OFFSET_A_INFO, sec_offset);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_led_set_count(adi_adpd6000_device_t *device, uint8_t slot, uint16_t num_int, uint16_t num_repeat)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_NUM_INT_A_INFO, num_int);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_NUM_REPEAT_A_INFO, num_repeat);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_adc_adjust(adi_adpd6000_device_t *device, uint8_t slot, uint16_t adjust1, uint16_t adjust2)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_CH1_ADC_ADJUST_A_INFO, adjust1);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_CH2_ADC_ADJUST_A_INFO, adjust2);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_minperiod(adi_adpd6000_device_t *device, uint8_t slot, uint16_t min_period)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_MIN_PERIOD_A_INFO, min_period);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_sel_precon(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_precon_e sel)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(sel > API_ADPD6000_PPG_PRECON_SHORT_DIFF_PAIR);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_PRECON_A_INFO, sel);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_sel_afe_path(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_afe_path_e sel)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(sel > API_ADPD6000_PPG_AFE_PATH_ADC);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_AFE_PATH_CFG_A_INFO, sel);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_config_vc(adi_adpd6000_device_t *device, uint8_t slot, uint8_t vc_index,
    adi_adpd6000_ppg_vc_select_e vc_sel, adi_adpd6000_ppg_vc_select_e vc_alt, adi_adpd6000_ppg_vc_pulse_e vc_pulse)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    ADPD6000_INVALID_PARAM_RETURN(vc_index > 1);
    ADPD6000_INVALID_PARAM_RETURN(vc_sel > API_ADPD6000_PPG_VC_GND);
    ADPD6000_INVALID_PARAM_RETURN(vc_alt > API_ADPD6000_PPG_VC_GND);
    ADPD6000_INVALID_PARAM_RETURN(vc_pulse > API_ADPD6000_PPG_VC_PULSE_FLOATING);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_VC1_SEL_A_INFO + 6 * vc_index, vc_sel);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_VC1_ALT_A_INFO + 6 * vc_index, vc_alt);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_VC1_PULSE_A_INFO + 6 * vc_index, vc_pulse);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_enable_channe2(adi_adpd6000_device_t *device, uint8_t slot, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_CH2_EN_A_INFO, enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_decimate(adi_adpd6000_device_t *device, uint8_t slot, bool enable, uint8_t decimate)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_SUBSAMPLE_A_INFO, enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_DECIMATE_FACTOR_A_INFO, decimate);
    ADPD6000_ERROR_RETURN(err);
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_ambientdac(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, uint16_t current)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + channel + BF_DAC_AMBIENT_CH1_A_INFO, current);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_dcdac(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, uint8_t current)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + channel + BF_DAC_LED_DC_CH1_A_INFO, current);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_data_size(adi_adpd6000_device_t *device, uint8_t slot, uint8_t signal_size, uint8_t lit_size, uint8_t dark_size)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_SIGNAL_SIZE_A_INFO, signal_size);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LIT_SIZE_A_INFO, lit_size);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_DARK_SIZE_A_INFO, dark_size);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_window_offset(adi_adpd6000_device_t *device, uint8_t slot, uint8_t signal_offset, uint8_t lit_offset, uint8_t dark_offset)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_SIGNAL_SHIFT_A_INFO, signal_offset);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_LIT_SHIFT_A_INFO, lit_offset);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_DARK_SHIFT_A_INFO, dark_offset);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_alctype(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_alc_type_e alc_type)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_AMBIENT_CANCELLATION_A_INFO, alc_type);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_alc_loop_width(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_alc_loop_width_e alc_loop_width)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(slot >= 12);

    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_COARSE_LOOP_WIDTH_A_INFO, alc_loop_width);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_set_sample_type(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_sample_type_e type)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(type > API_ADPD6000_PPG_SAMPLE_TYPE_IMPULSE);
    
    err = adi_adpd6000_hal_bf_write(device, ADPD6000_TIME_SLOT_SPAN * slot + BF_SAMPLE_TYPE_A_INFO, type);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_read_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *signal_data, uint32_t *dark_data, uint32_t *lit_data, uint8_t *ppg_num)
{
    int32_t  err;  
    uint8_t i, j, k, ppg_count = 0;
    uint8_t fifo_data[4];
    uint32_t data;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    for (i = 0; i < fifo->ppg_slot; i++)
    {      
        for (j = 0; j <= fifo->ppg_fifo[i].ppg_chl2_en; j++)
        {
            ppg_count++;
            if (fifo->ppg_fifo[i].signal_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].signal_size);
                ADPD6000_ERROR_RETURN(err);
                if (signal_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].signal_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].signal_size - 1 - k) * 8));
                    }
                    *signal_data++ = data;
                }
            }
            if (fifo->ppg_fifo[i].dark_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].dark_size);
                ADPD6000_ERROR_RETURN(err);
                if (dark_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].dark_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].dark_size - 1 - k) * 8));
                    }
                    *dark_data++ = data;
                }
            }
            if (fifo->ppg_fifo[i].lit_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].lit_size);
                ADPD6000_ERROR_RETURN(err);
                if (lit_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].lit_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].lit_size - 1 - k) * 8));
                    }
                    *lit_data++ = data;
                }
            }
        }
    }
    *ppg_num = ppg_count;
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_read_struct_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_slot_data_t *signal_data, adi_adpd6000_ppg_slot_data_t *dark_data, adi_adpd6000_ppg_slot_data_t *lit_data, uint8_t *slot_num)
{
    int32_t  err;  
    uint8_t i, j, k;
    uint8_t fifo_data[4];
    uint32_t data;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    for (i = 0; i < fifo->ppg_slot; i++)
    {      
        for (j = 0; j <= fifo->ppg_fifo[i].ppg_chl2_en; j++)
        {
            if (fifo->ppg_fifo[i].signal_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].signal_size);
                ADPD6000_ERROR_RETURN(err);
                if (signal_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].signal_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].signal_size - 1 - k) * 8));
                    }
                    signal_data[i].chnl[j] = data;
                }
            }
            if (fifo->ppg_fifo[i].dark_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].dark_size);
                ADPD6000_ERROR_RETURN(err);
                if (dark_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].dark_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].dark_size - 1 - k) * 8));
                    }
                    dark_data[i].chnl[j] = data;
                }
            }
            if (fifo->ppg_fifo[i].lit_size > 0)
            {
                err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ppg_fifo[i].lit_size);
                ADPD6000_ERROR_RETURN(err);
                if (lit_data != NULL)
                {
                    data = 0;
                    for (k = 0; k < fifo->ppg_fifo[i].lit_size; k++)
                    {
                        data += (fifo_data[k] << ((fifo->ppg_fifo[i].lit_size - 1 - k) * 8));
                    }
                    lit_data[i].chnl[j] = data;
                }
            }
        }
    }
    *slot_num = fifo->ppg_slot;
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_agc_init(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_agc_cfg_t *ppg_agc_cfg)
{
    int32_t err;
    uint8_t i;
    uint16_t data;
    ppg_sample_count = 0;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_NULL_POINTER_RETURN(fifo);
    ADPD6000_NULL_POINTER_RETURN(ppg_agc_cfg);
    ADPD6000_INVALID_PARAM_RETURN(ppg_agc_cfg->ppg_average_sample_number == 0);

    for (i = 0; i < fifo->ppg_slot; i++)
    {
        ppg_agc_run[i].ppg_data_sum = 0;
        ppg_agc_run[i].agc_done = 0;
        
        err = adi_adpd6000_hal_bf_read(device, ADPD6000_TIME_SLOT_SPAN * i + BF_NUM_INT_A_INFO, &data);
        ADPD6000_ERROR_RETURN(err);
        ppg_agc_run[i].ppg_full_scale = 16383 * data;
        err = adi_adpd6000_hal_bf_read(device, ADPD6000_TIME_SLOT_SPAN * i + BF_NUM_REPEAT_A_INFO, &data);
        ADPD6000_ERROR_RETURN(err);
        ppg_agc_run[i].ppg_full_scale = ppg_agc_run[i].ppg_full_scale * data;

        err = adi_adpd6000_hal_bf_read(device, ADPD6000_TIME_SLOT_SPAN * i + BF_TIA_GAIN_CH1_A_INFO, &data);
        ADPD6000_ERROR_RETURN(err);
        ppg_agc_run[i].tia_gain = data;
        err = adi_adpd6000_hal_bf_read(device, ADPD6000_TIME_SLOT_SPAN * i + BF_LED_CURRENT1_A_INFO + 8 * ppg_agc_cfg->slot[i].led_chnl, &data);
        ADPD6000_ERROR_RETURN(err);
        ppg_agc_run[i].led_current = data;
    }
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ppg_agc_process(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_agc_cfg_t *ppg_agc_cfg, uint32_t *signal_data)
{
    int32_t err;
    uint8_t i;
    uint32_t data;
    float step;
    float ppg_data_avg, ppg_chnl_current;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_NULL_POINTER_RETURN(fifo);
    ADPD6000_NULL_POINTER_RETURN(ppg_agc_cfg);
    ADPD6000_NULL_POINTER_RETURN(signal_data);
    
    ppg_sample_count++;
    if (ppg_sample_count <= ppg_agc_cfg->ppg_skip_sample_number)  
    {
        return API_ADPD6000_ERROR_OK;
    }
    if (ppg_sample_count <= (ppg_agc_cfg->ppg_skip_sample_number + ppg_agc_cfg->ppg_average_sample_number))
    {
        for (i = 0; i < fifo->ppg_slot; i++)
        {
            if (fifo->ppg_fifo[i].ppg_chl2_en == 0)
            {
                ppg_agc_run[i].ppg_data_sum += *signal_data++;
            }
            else
            {
                if (ppg_agc_cfg->slot[i].tia_chnl == 0)
                {
                    ppg_agc_run[i].ppg_data_sum += *signal_data++;
                    data = *signal_data++;
                }
                else if (ppg_agc_cfg->slot[i].tia_chnl == 1)
                {
                    data = *signal_data++;
                    ppg_agc_run[i].ppg_data_sum += *signal_data++;
                }
            }
        }
        data = data;
        return API_ADPD6000_ERROR_OK;
    }
    
    for (i = 0; i < fifo->ppg_slot; i++)
    {
        if ((ppg_agc_cfg->slot[i].agc_en == 0) || (ppg_agc_run[i].agc_done == 1))
        {
            continue;
        }
        ppg_data_avg = ppg_agc_run[i].ppg_data_sum / ppg_agc_cfg->ppg_average_sample_number;
        ppg_data_avg ++;
        ppg_chnl_current = (float)(ppg_agc_run[i].led_current * ppg_agc_run[i].ppg_full_scale * 0.5 / ppg_data_avg);
        if (ppg_chnl_current > 127)
        {         
            if(ppg_agc_run[i].tia_gain > 0)
            {
                ppg_agc_run[i].tia_gain = ppg_agc_run[i].tia_gain - 1;
                ppg_chnl_current = ppg_agc_run[i].led_current; 
            }
            else
            {
                ppg_chnl_current = ppg_agc_run[i].led_current + 0x10;
                ppg_chnl_current = (ppg_chnl_current > 127) ? 127 : ppg_chnl_current;
            }
        }
        else if (ppg_chnl_current < 1)
        {
            if(ppg_agc_run[i].tia_gain < 5)
            {
                ppg_agc_run[i].tia_gain = ppg_agc_run[i].tia_gain + 1;
                ppg_chnl_current =  ppg_agc_run[i].led_current;
            }
            else
            {
                ppg_chnl_current = (ppg_agc_run[i].led_current > 0x11) ? (ppg_agc_run[i].led_current - 0x10) : 1; 
            }
        }
        
        if (ppg_agc_cfg->power_first_en == 0)    /* low tia gain */ 
        {
            if (fabs(ppg_agc_run[i].led_current - (uint8_t)ppg_chnl_current) > 1)
            {
                ppg_agc_run[i].led_current = (uint8_t)ppg_chnl_current;
            }
            else
            {
                if (ppg_agc_cfg->slot[i].agc_type == 1)
                {
                    ppg_agc_run[i].agc_done = 1;
                }
            }

            if ((ppg_agc_run[i].led_current < 0x20) && (ppg_agc_run[i].tia_gain < 5))
            {
                ppg_agc_run[i].led_current = ppg_agc_run[i].led_current * 2;
                ppg_agc_run[i].tia_gain = ppg_agc_run[i].tia_gain + 1;
            }
        }
        else                                    /* low LED current */  
        {
            step = fabs(ppg_chnl_current - ppg_agc_run[i].led_current) / 4;
            if (step < 1)
            {
                step = 1;
            } 
            if (ppg_agc_run[i].led_current > ((uint8_t)ppg_chnl_current + 1))
            {
                ppg_agc_run[i].led_current -= (uint8_t)step;
            }
            else if (ppg_agc_run[i].led_current < ((uint8_t)ppg_chnl_current - 1))
            {
                ppg_agc_run[i].led_current += (uint8_t)step;
            }
            if ((ppg_agc_run[i].led_current > 0x10) && (ppg_agc_run[i].tia_gain > 2))
            {
                ppg_agc_run[i].led_current = ppg_agc_run[i].led_current / 2;
                ppg_agc_run[i].tia_gain = ppg_agc_run[i].tia_gain - 1;
            }
        }
    }

    adi_adpd6000_device_enable_slot_operation_mode_go(device, false);
    for (i = 0; i < fifo->ppg_slot; i++)
    {      
        if (ppg_agc_cfg->slot[i].agc_en == 1)
        {
            err = adi_adpd6000_ppg_tia_set_gain_res(device, i, ppg_agc_cfg->slot[i].tia_chnl, (adi_adpd6000_ppg_tia_gain_res_e)ppg_agc_run[i].tia_gain);
            ADPD6000_ERROR_RETURN(err);
            err = adi_adpd6000_ppg_led_set_current(device, i, ppg_agc_cfg->slot[i].led_chnl, ppg_agc_run[i].led_current);
            ADPD6000_ERROR_RETURN(err);
        }
    }
    adi_adpd6000_device_clr_fifo(device);
    adi_adpd6000_device_enable_slot_operation_mode_go(device, true);
    
    ppg_sample_count = 0;
    for (i = 0; i < fifo->ppg_slot; i++)
    {
        ppg_agc_run[i].ppg_data_sum = 0;;
    }
    
    return err;
}
/*! @} */
