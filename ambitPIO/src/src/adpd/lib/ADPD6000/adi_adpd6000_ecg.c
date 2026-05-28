/*!
 * @brief     ECG APIs Implementation
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */

/*!
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

/*============= I N C L U D E S ============*/
#include "adi_adpd6000.h"

/*============= D E F I N E S ==============*/

/*============= D A T A ====================*/

/*============= C O D E ====================*/
int32_t adi_adpd6000_ecg_enable_slot(adi_adpd6000_device_t *device, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_TIMESLOT_EN_INFO, enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_set_input_mux(adi_adpd6000_device_t *device, bool input_en, bool short_en)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_INP_CONNECT_INFO , input_en ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_SHORT_INFO , short_en ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_set_odr(adi_adpd6000_device_t *device, adi_adpd6000_ecg_odr_e odr)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(odr > API_ADPD6000_ECG_ODR_4000);

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_ODR_SEL_INFO , odr);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_set_oversample(adi_adpd6000_device_t *device, uint8_t oversample)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_OVERSAMPLING_RATIO_INFO , oversample);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_enable_statusbyte(adi_adpd6000_device_t *device, bool enable)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    err = adi_adpd6000_hal_bf_write(device, BF_ENA_STAT_ECG_INFO , enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_get_ecg_status(adi_adpd6000_device_t *device, uint8_t *status)
{
    int32_t  err;
    uint16_t data;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_NULL_POINTER_RETURN(status);

    err = adi_adpd6000_hal_reg_read(device, REG_ECG_STATUS_ADDR, &data);
    ADPD6000_ERROR_RETURN(err);
    *status = (uint8_t)data;

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_clr_ecg_status(adi_adpd6000_device_t *device, uint8_t status)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();

    err = adi_adpd6000_hal_reg_write(device, REG_ECG_STATUS_ADDR, status);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_set_rld(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_rld_output_e rld)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(rld > API_ADPD6000_ECG_RLD_OUTPUT_CM_INPUT);

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_RLD_EN_INFO , enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_RLD_OUT_SEL_INFO , rld);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_leadoff_set_rld(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_dc_leadoff_th_e th)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(th > API_ADPD6000_ECG_DC_LEADOFF_400MV);

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_RLD_SAT_EN_INFO , enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_RLD_SAT_THRESHOLD_INFO , th);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_leadoff_set_ac(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_ac_leadoff_mag_e mag, uint8_t th)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(mag > API_ADPD6000_ECG_AC_LEADOFF_70NA);

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_ACLO_EN_INFO , enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_ACLO_MAG_INFO , mag);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_ACLO_THRESHOLD_INFO , th);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_leadoff_set_dc(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_dc_leadoff_mag_e mag,
    adi_adpd6000_ecg_dc_leadoff_th_e th, adi_adpd6000_ecg_dc_leadoff_pol_e in_pol,  adi_adpd6000_ecg_dc_leadoff_pol_e ip_pol)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_INVALID_PARAM_RETURN(mag > API_ADPD6000_ECG_DC_LEADOFF_16NA);

    err = adi_adpd6000_hal_bf_write(device, BF_ECG_DCLO_EN_INFO , enable ? 1 : 0);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_DCLO_MAG_INFO , mag);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_DCLO_THRESHOLD_INFO , th);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_DCLO_POLARITY_IN_INFO , in_pol);
    ADPD6000_ERROR_RETURN(err);
    err = adi_adpd6000_hal_bf_write(device, BF_ECG_DCLO_POLARITY_IP_INFO , ip_pol);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_read_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *ecg_data, uint8_t *ecg_num)
{
    int32_t err;  
    uint8_t i, ecg_count = 0;;
    uint8_t fifo_data[4];
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_NULL_POINTER_RETURN(fifo);
    ADPD6000_NULL_POINTER_RETURN(ecg_data);
    ADPD6000_NULL_POINTER_RETURN(ecg_num);

    if (fifo->ecg_slot)
    {
        for (i = 0; i < fifo->ecg_over_sample; i++)
        {
            err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ecg_size);
            ADPD6000_ERROR_RETURN(err);
            if ((fifo_data[0] == 0xff) && (fifo->ecg_size == 4) && ((fifo->ppg_slot != 0) || (fifo->bioz_slot != 0)))
            {
               continue;
            }
            else
            { 
                if (fifo->ecg_size == 4) 
                {
                    *ecg_data++ = (fifo_data[0] << 24) || (fifo_data[1] << 16) | (fifo_data[2] << 8) | (fifo_data[3]);
                }
                else
                {
                    *ecg_data++ = ((fifo_data[0] << 16) | (fifo_data[1] << 8) | (fifo_data[2]));
                }
                ecg_count++;
            }
        }
    }
    *ecg_num = ecg_count;
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_ecg_read_data_status(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *ecg_data, uint8_t *status, uint8_t *ecg_num)
{
    int32_t err;  
    uint8_t i, ecg_count = 0;;
    uint8_t fifo_data[4];
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_LOG_FUNC();
    ADPD6000_NULL_POINTER_RETURN(fifo);
    ADPD6000_NULL_POINTER_RETURN(ecg_data);
    ADPD6000_NULL_POINTER_RETURN(ecg_num);

    if (fifo->ecg_slot)
    {
        for (i = 0; i < fifo->ecg_over_sample; i++)
        {
            err = adi_adpd6000_device_fifo_read_bytes(device, fifo_data, fifo->ecg_size);
            ADPD6000_ERROR_RETURN(err);
            if ((fifo_data[0] == 0xff) && (fifo->ecg_size == 4) && ((fifo->ppg_slot != 0) || (fifo->bioz_slot != 0)))
            {
               continue;
            }
            else
            { 
                if (fifo->ecg_size == 4) 
                {
                    *ecg_data++ = (fifo_data[1] << 16) | (fifo_data[2] << 8) | (fifo_data[3]);
                    if (status != NULL)
                    {
                        *status++ = fifo_data[0];
                    }
                }
                else
                {
                    *ecg_data++ = ((fifo_data[0] << 16) | (fifo_data[1] << 8) | (fifo_data[2]));
                }
                ecg_count++;
            }
        }
    }
    *ecg_num = ecg_count;
    
    return API_ADPD6000_ERROR_OK;
}
/*! @} */
