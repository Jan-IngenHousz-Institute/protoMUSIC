/*!
 * @brief     Hardare Abstract Layer Implementation
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */
 
/*!
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

/*============= I N C L U D E S ============*/
#include "adi_adpd6000.h"

/*============= C O D E ====================*/
int32_t adi_adpd6000_hal_reg_read(adi_adpd6000_device_t *device, uint32_t reg_addr, uint16_t *reg_data)
{
    int32_t err;
    uint32_t address;
    uint8_t wr_buf[ADPD6000_SDK_MAX_BUFSIZE] = {0};
    uint8_t rd_buf[ADPD6000_SDK_MAX_BUFSIZE] = {0};
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_NULL_POINTER_RETURN(reg_data);
    
    address = (reg_addr << 1);
    wr_buf[0] = ((address  >> 8)  & 0xFF);  /* address [15:08] */
    wr_buf[1] = ((address      )  & 0xFF);  /* address [07:00] */
    
    err = device->read(device->user_data, rd_buf, 2, wr_buf, 2);
    ADPD6000_ERROR_RETURN(err);

    *reg_data = rd_buf[1] + (rd_buf[0] << 8);
    
    ADPD6000_LOG_REG("r@%.8x = %.8x", reg_addr, *reg_data);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_reg_write(adi_adpd6000_device_t *device, uint32_t reg_addr, uint16_t reg_data)
{
    int32_t err;
    uint32_t address;
    uint8_t wr_buf[ADPD6000_SDK_MAX_BUFSIZE] = {0};
    ADPD6000_NULL_POINTER_RETURN(device);
    
    address = (reg_addr << 1) + 1;
    wr_buf[0] = ((address  >> 8)  & 0xFF);  /* address [15:08] */
    wr_buf[1] = ((address      )  & 0xFF);  /* address [07:00] */
    wr_buf[2] = ((reg_data >> 8)  & 0xFF);  /* data    [15:08] */
    wr_buf[3] = ((reg_data     )  & 0xFF);  /* data    [07:00] */
    
    err = device->write(device->user_data, wr_buf, 4);
    ADPD6000_ERROR_RETURN(err);
    ADPD6000_LOG_REG("w@%.8x = %.8x", reg_addr, reg_data);
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_bf_read(adi_adpd6000_device_t *device, uint32_t reg_addr, uint32_t bf_info, uint16_t *bf_val)
{
    uint16_t reg_value, reg_mask;
    int32_t  err;
    uint8_t  bit_start = bf_info;
    uint8_t  bit_count = bf_info >> 8;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_NULL_POINTER_RETURN(bf_val);
    ADPD6000_INVALID_PARAM_RETURN((bit_count > 16) || (bit_count == 0));

    err = adi_adpd6000_hal_reg_read(device, reg_addr, &reg_value);
    ADPD6000_ERROR_RETURN(err);
    if((bit_count + bit_start) <= 16)
    {
        reg_mask = (bit_count == 16) ? 0xffff : ((1 << bit_count) - 1);
        *bf_val  = (reg_value >> bit_start) & reg_mask;
    }
    else
    {
        *bf_val = reg_value >> bit_start;
        err = adi_adpd6000_hal_reg_read(device, reg_addr + 4, &reg_value);
        ADPD6000_ERROR_RETURN(err);
        reg_mask = (1 << (bit_count - (16 - bit_start))) - 1;
        *bf_val += (reg_value & reg_mask) << (16 - bit_start);
    }
    
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_bf_write(adi_adpd6000_device_t *device, uint32_t reg_addr, uint32_t bf_info, uint16_t bf_val)
{
    uint16_t reg_value, reg_mask;
    int32_t  err;
    uint8_t  bit_start = bf_info;
    uint8_t  bit_count = bf_info >> 8;
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_INVALID_PARAM_RETURN((bit_count > 16) || (bit_count == 0));

    if ((bit_count == 16) && (bit_start == 0))
    {
        err = adi_adpd6000_hal_reg_write(device, reg_addr, bf_val);
        ADPD6000_ERROR_RETURN(err);
    }
    else
    {
        err = adi_adpd6000_hal_reg_read(device, reg_addr, &reg_value);
        ADPD6000_ERROR_RETURN(err);
        if((bit_count + bit_start) <= 16)
        {
            reg_mask = (bit_count == 16) ? 0xffff : ((1 << bit_count) - 1);
            reg_value &= ~(reg_mask << bit_start);
            reg_value |=  (bf_val   << bit_start);
            err = adi_adpd6000_hal_reg_write(device, reg_addr, reg_value);
            ADPD6000_ERROR_RETURN(err);
        }
        else
        {
            reg_mask = 0xffff;
            reg_value &= ~(reg_mask << bit_start);
            reg_value |=  (bf_val   << bit_start);
            err = adi_adpd6000_hal_reg_write(device, reg_addr, reg_value);
            ADPD6000_ERROR_RETURN(err);
            err = adi_adpd6000_hal_reg_read(device, reg_addr + 4, &reg_value);
            ADPD6000_ERROR_RETURN(err);
            reg_mask = (1 << (bit_count - (16 - bit_start))) - 1;
            reg_value &= ~reg_mask;
            reg_value |= (bf_val >> (16 - bit_start));
            err = adi_adpd6000_hal_reg_write(device, reg_addr, reg_value);
            ADPD6000_ERROR_RETURN(err);
        }
    }
        
    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_log_write(adi_adpd6000_device_t *device, uint32_t log_type, const char* comment, ...)
{
    #if ((ADPD6000_REPORT_VERBOSE & 0x0000ffff) > 0) && ((ADPD6000_REPORT_VERBOSE & 0xffff0000) > 0)
    va_list argp;
    char *msg_header = NULL;
    static char logmsgbuf[100];
    if (device == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if (comment == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if ((device->log_write != NULL) && ((ADPD6000_REPORT_VERBOSE & log_type & 0x0000ffff) > 0) && ((ADPD6000_REPORT_VERBOSE & log_type & 0xffff0000) > 0))
    {
        va_start(argp, comment);
        if (((ADPD6000_REPORT_VERBOSE & ADPD6000_LOG_ERR_MSG)  > 0) && ((log_type & ADPD6000_LOG_ERR_MSG)  > 0))
            msg_header = "ERR ";
        if (((ADPD6000_REPORT_VERBOSE & ADPD6000_LOG_WARN_MSG) > 0) && ((log_type & ADPD6000_LOG_WARN_MSG) > 0))
            msg_header = "WARN";
        if (((ADPD6000_REPORT_VERBOSE & ADPD6000_LOG_INFO_MSG) > 0) && ((log_type & ADPD6000_LOG_INFO_MSG) > 0))
            msg_header = "INFO";
        if (sprintf(logmsgbuf, "%s: ", msg_header) < 0)
            return API_ADPD6000_ERROR_LOG_WRITE;
        if (vsprintf(logmsgbuf + 6, comment, argp) < 0)
            return API_ADPD6000_ERROR_LOG_WRITE;
        if (API_ADPD6000_ERROR_OK != device->log_write(device->user_data, logmsgbuf))
            return API_ADPD6000_ERROR_LOG_WRITE;
        va_end(argp);
    }
    #endif

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_error_report(adi_adpd6000_device_t* device, uint32_t log_type, const char* file_name, const char* func_name, uint32_t line_num, const char* var_name, const char* comment)
{
    if (device == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if (file_name == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if (func_name == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if (var_name == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;
    if (comment == NULL)
        return API_ADPD6000_ERROR_NULL_PARAM;

    if (API_ADPD6000_ERROR_OK != adi_adpd6000_hal_log_write(device, log_type, "%s, \"%s\" in %s(...), line%d in %s", 
        comment, var_name, func_name, line_num, file_name))
        return API_ADPD6000_ERROR_LOG_WRITE;

    return API_ADPD6000_ERROR_OK;
}

int32_t adi_adpd6000_hal_fifo_read_bytes(adi_adpd6000_device_t *device, uint32_t reg_addr, uint8_t *reg_data, uint32_t len)
{
    int32_t err;
    uint32_t address;
    uint8_t wr_buf[ADPD6000_SDK_MAX_BUFSIZE] = {0};
    ADPD6000_NULL_POINTER_RETURN(device);
    ADPD6000_NULL_POINTER_RETURN(reg_data);
    
    address = (reg_addr << 1);
    wr_buf[0] = ((address  >> 8)  & 0xFF);  /* address [15:08] */
    wr_buf[1] = ((address      )  & 0xFF);  /* address [07:00] */
    
    err = device->read(device->user_data, reg_data, len, wr_buf, 2);
    ADPD6000_ERROR_RETURN(err);

    return API_ADPD6000_ERROR_OK;
}

/*! @} */
