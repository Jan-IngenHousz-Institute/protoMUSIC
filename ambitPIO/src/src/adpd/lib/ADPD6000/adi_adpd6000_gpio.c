/*!
 * @brief     BIOZ APIs Implementation
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */

/*!
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

/*============= I N C L U D E S ============*/
#include "adi_adpd6000.h"

/*============= D E F I N E S ==============*/

/*============= C O D E ====================*/
int32_t adi_adpd6000_gpio_set_mode(adi_adpd6000_device_t *device, uint8_t index, adi_adpd6000_gpio_mode_e mode)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_INVALID_PARAM_RETURN(index > 2);
    ADPD6000_LOG_FUNC();
    
    err = adi_adpd6000_hal_bf_write(device, BF_GPIO_PIN_CFG0_INFO + 3 * index, mode);
    ADPD6000_ERROR_RETURN(err);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_gpio_set_output(adi_adpd6000_device_t *device, uint8_t index, uint8_t level)
{
    int32_t  err;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_INVALID_PARAM_RETURN(index > 2);
    ADPD6000_LOG_FUNC();
    
    if (index < 2)
    {
        err = adi_adpd6000_hal_bf_write(device, BF_GPIOOUT0_INFO + 8 * index, level);
        ADPD6000_ERROR_RETURN(err);
    }
    else
    {
        err = adi_adpd6000_hal_bf_write(device, BF_GPIOOUT2_INFO, level);
        ADPD6000_ERROR_RETURN(err);
    }
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_gpio_get_input(adi_adpd6000_device_t *device, uint8_t index, uint8_t *level)
{
    int32_t  err;
    uint16_t data;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_NULL_POINTER_RETURN(level);
    ADPD6000_INVALID_PARAM_RETURN(index > 2);
    ADPD6000_LOG_FUNC();
    
    err = adi_adpd6000_hal_bf_read(device, BF_GPIO_INPUT_INFO, &data);
    ADPD6000_ERROR_RETURN(err);
    
    *level = (data >> index) & 0x01;
    
    return API_ADPD6000_ERROR_OK;
}

/*! @} */
