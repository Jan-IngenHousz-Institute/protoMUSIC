/*! 
 * @brief     adi adpd6000 header file
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */
 
/*! 
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

#ifndef __ADI_ADPD6000_H__
#define __ADI_ADPD6000_H__

/*============= I N C L U D E S ============*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#if ADPD6000_REPORT_VERBOSE > ADPD6000_LOG_NONE_MSG
#include <stdarg.h>
#include <stdio.h>
#endif
#include "adi_adpd6000_bf_reg.h"
#include "adi_adpd6000_config.h"

/*============= D E F I N E S ==============*/
/*!
 * @brief SDK version macro (major.minor.build, one byte each)
 */
#define ADPD6000_SDK_VER            (0x00010300)

/*!
 * @brief Address macros
 */
#define ADPD6000_TIME_SLOT_SPAN     ((REG_TS_CTRL_B_ADDR) - (REG_TS_CTRL_A_ADDR))


/*!
 * @brief SDK message report macro
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #define __FUNCTION_NAME__ __func__
#else
    #define __FUNCTION_NAME__ __FUNCTION__
#endif

#if ADPD6000_REPORT_VERBOSE > ADPD6000_LOG_NONE_MSG
#define ADPD6000_MSG_REPORT(var, comment) \
    adi_adpd6000_hal_error_report(device, ADPD6000_LOG_VAR_MSG | ADPD6000_LOG_INFO_MSG, __FILE__, __FUNCTION_NAME__, __LINE__, #var, comment)
#define ADPD6000_WARN_REPORT(var, comment) \
    adi_adpd6000_hal_error_report(device, ADPD6000_LOG_VAR_MSG | ADPD6000_LOG_WARN_MSG, __FILE__, __FUNCTION_NAME__, __LINE__, #var, comment)
#define ADPD6000_ERROR_REPORT(var, comment) \
    adi_adpd6000_hal_error_report(device, ADPD6000_LOG_VAR_MSG | ADPD6000_LOG_ERR_MSG,  __FILE__, __FUNCTION_NAME__, __LINE__, #var, comment)
#define ADPD6000_LOG_MSG(msg) \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_MISC_MSG | ADPD6000_LOG_INFO_MSG, msg)
#define ADPD6000_LOG_WARN(msg) \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_MISC_MSG | ADPD6000_LOG_WARN_MSG, msg)
#define ADPD6000_LOG_ERR(msg) \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_MISC_MSG | ADPD6000_LOG_ERR_MSG,  msg)
#define ADPD6000_LOG_FUNC() \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_FUNC_MSG | ADPD6000_LOG_INFO_MSG, "%s(...)", __FUNCTION_NAME__)
#define ADPD6000_LOG_REG(msg, ...) \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_REG_MSG  | ADPD6000_LOG_INFO_MSG, msg, ##__VA_ARGS__)
#define ADPD6000_LOG_VAR(type, msg, ...) \
    adi_adpd6000_hal_log_write(device, ADPD6000_LOG_VAR_MSG  | type, msg, ##__VA_ARGS__)
#else
#define ADPD6000_MSG_REPORT(var, comment)
#define ADPD6000_WARN_REPORT(var, comment)
#define ADPD6000_ERROR_REPORT(var, comment)
#define ADPD6000_LOG_MSG(msg)
#define ADPD6000_LOG_WARN(msg)
#define ADPD6000_LOG_ERR(msg)
#define ADPD6000_LOG_FUNC()
#define ADPD6000_LOG_VAR(type, msg, ...)
#define ADPD6000_LOG_REG(msg, ...)
#endif

/*!
 * @brief error code check macro, return error code if it is not API_ADPD6000_ERROR_OK
 */
#define ADPD6000_ERROR_RETURN(r) \
{ \
    if ((r) != API_ADPD6000_ERROR_OK) { \
        return r; \
    } \
}

/*!
 * @brief param check macro, return API_ADPD6000_ERROR_NULL_PARAM if param is NULL
 */
#define ADPD6000_NULL_POINTER_RETURN(p) \
{ \
    if((p) == NULL) { \
        ADPD6000_ERROR_REPORT(p, "Null pointer passed."); \
        return API_ADPD6000_ERROR_NULL_PARAM; \
    } \
}

/*!
 * @brief param check macro, return API_ADPD6000_ERROR_INVALID_PARAM if equation is true
 */
#define ADPD6000_INVALID_PARAM_RETURN(r) \
{ \
    if(r) { \
        ADPD6000_ERROR_REPORT(r, "Invalid param passed."); \
        return API_ADPD6000_ERROR_INVALID_PARAM; \
    } \
}

/*!
 * @brief param check macro, log warning message if equation is true
 */
#define ADPD6000_INVALID_PARAM_WARN(r) \
{ \
    if(r) { \
        ADPD6000_WARN_REPORT(r, "Invalid param passed."); \
    } \
}

/**
 * @brief  Platform dependent control port read function.
 *
 * @param  user_data    Pointer to customer data if needed, usually handle to a specific spi/i2c/uart
 * @param  rd_buf       Pointer to data returned from read operation
 * @param  rd_len       Length to read, in bytes
 * @param  wr_buf       Pointer to write data required during read operation
 * @param  wr_len       Length to write, in bytes
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
typedef int32_t (*adi_adpd6000_read)(void* user_data, uint8_t *rd_buf, uint32_t rd_len, uint8_t *wr_buf, uint32_t wr_len);

/**
 * @brief  Platform dependent control port write function.
 *
 * @param  user_data    Pointer to customer data if needed, usually handle to a specific spi/i2c/uart device
 * @param  wr_buf       Pointer to write data required during read operation
 * @param  len          Length to write, in bytes
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
typedef int32_t (*adi_adpd6000_write)(void* user_data, uint8_t *wr_buf, uint32_t len);

/**
 * @brief  Platform dependent log write function.
 *         
 * @param  user_data  Pointer to customer data if needed
 * @param  string     Messsage to log
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
typedef int32_t (*adi_adpd6000_log_write)(void* user_data, char *string);

/*!
 * @brief  adi adpd6000 device structure
 */
typedef struct
{
    void*                   user_data;                          /*!< User structure */
    adi_adpd6000_read      read;                               /*!< Function Pointer to HAL SPI read function */
    adi_adpd6000_write     write;                              /*!< Function Pointer to HAL SPI write function */
    adi_adpd6000_log_write log_write;                          /*!< Function Pointer to HAL log write function */
} adi_adpd6000_device_t;

/*!
 * @brief  adpd6000 ppg fifo information
 */
typedef struct
{
    uint8_t ppg_chl2_en;                                        /*!< 0 - Channel2 disabled, 1 - Channel2 enabled */
    uint8_t signal_size;                                        /*!< size of singnal data */
    uint8_t dark_size;                                          /*!< size of dark data */
    uint8_t lit_size;                                           /*!< size of lit data */
} adi_adpd6000_ppg_fifo_config_t;

/*!
 * @brief  adpd6000 ppg fifo data
 */
typedef struct
{
    uint32_t chnl[2];                                             /*!< Channel 1&2 */
} adi_adpd6000_ppg_slot_data_t;

/*!
 * @brief  adpd6000 sequence fifo information
 */
typedef struct
{
    uint32_t sequence_size;                                     /*!< Size of data in FIFO druing a sequence */
    uint8_t ecg_slot;                                           /*!< ECG slot number, 0 or 1 */
    uint8_t ecg_over_sample;                                    /*!< ECG oversampling rate */
    uint8_t ecg_size;                                           /*!< ECG data size, 3 bytes - status disabled, 4 bytes - status byte enabled */
    uint8_t ppg_slot;                                           /*!< PPG slot number */
    uint8_t ppg_chnl_num;                                       /*!< PPG channel number */
    adi_adpd6000_ppg_fifo_config_t ppg_fifo[12];               /*!< PPG FIFO configuration */
    uint8_t bioz_slot;                                          /*!< BioZ slot number */
} adi_adpd6000_fifo_config_t;

/*!
 * @brief  return value for adi adpd6000 api
 */
typedef enum {
    API_ADPD6000_ERROR_OK                       =  0,           /*!< No Error */
    API_ADPD6000_ERROR_ERROR                    = -1,           /*!< General Error  */
    API_ADPD6000_ERROR_NULL_PARAM               = -2,           /*!< Null parameter */
    API_ADPD6000_ERROR_INVALID_PARAM            = -3,           /*!< Invalid parameter passed */
    API_ADPD6000_ERROR_NOT_SUPPORTED            = -4,           /*!< Not supported */
    API_ADPD6000_ERROR_REG_ACCESS               = -5,           /*!< Register access error */
    API_ADPD6000_ERROR_LOG_WRITE                = -6,           /*!< Log write error */
    API_ADPD6000_ERROR_FUSE_NOT_DONE            = -7,           /*!< Fuse not done */
} adi_adpd6000_error_e;

/*!
 * @brief  GPIO mode enumuration
 */
typedef enum {
    API_ADPD6000_GPIO_MODE_DISABLE              =  0,           /*!< Disabled (tristate, input buffer off) */
    API_ADPD6000_GPIO_MODE_INPUT                =  1,           /*!< Enabled Input */
    API_ADPD6000_GPIO_MODE_NORMAL               =  2,           /*!< Output - Normal */
    API_ADPD6000_GPIO_MODE_INVERTED             =  3,           /*!< Output - Inverted */
    API_ADPD6000_GPIO_MODE_PD_NORMAL            =  4,           /*!< Pulldown Only - Normal */
    API_ADPD6000_GPIO_MODE_PD_INVERTED          =  5,           /*!< Pulldown Only - Inverted */
    API_ADPD6000_GPIO_MODE_PU_NORMAL            =  6,           /*!< Pullup Only - Normal */
    API_ADPD6000_GPIO_MODE_PU_INVERTED          =  7,           /*!< Pullup Only - Inverted */
} adi_adpd6000_gpio_mode_e;

/*!
 * @brief  Time slot enumuration
*/
typedef enum {
    API_ADPD6000_SLOT_A = 0,                                    /*!< Timeslot A */
    API_ADPD6000_SLOT_B = 1,                                    /*!< Timeslot B */
    API_ADPD6000_SLOT_C = 2,                                    /*!< Timeslot C */
    API_ADPD6000_SLOT_D = 3,                                    /*!< Timeslot D */
    API_ADPD6000_SLOT_E = 4,                                    /*!< Timeslot E */
    API_ADPD6000_SLOT_F = 5,                                    /*!< Timeslot F */
    API_ADPD6000_SLOT_G = 6,                                    /*!< Timeslot G */
    API_ADPD6000_SLOT_H = 7,                                    /*!< Timeslot H */
    API_ADPD6000_SLOT_I = 8,                                    /*!< Timeslot I */
    API_ADPD6000_SLOT_J = 9,                                    /*!< Timeslot J */
    API_ADPD6000_SLOT_K = 10,                                   /*!< Timeslot K */
    API_ADPD6000_SLOT_L = 11,                                   /*!< Timeslot L */
} adi_adpd6000_slot_e;

/*!
 * @brief  PPG slot mode enumuration
*/
typedef enum {
    API_ADPD6000_PPG_SLOT_NONE         = 0,                     /*!< No PPG timeslot */
    API_ADPD6000_PPG_SLOT_A            = 1,                     /*!< PPG timeslot sequency A only */
    API_ADPD6000_PPG_SLOT_AB           = 2,                     /*!< PPG timeslot sequency AB */
    API_ADPD6000_PPG_SLOT_ABC          = 3,                     /*!< PPG timeslot sequency ABC */
    API_ADPD6000_PPG_SLOT_ABCD         = 4,                     /*!< PPG timeslot sequency ABCD */
    API_ADPD6000_PPG_SLOT_ABCDE        = 5,                     /*!< PPG timeslot sequency ABCDE */
    API_ADPD6000_PPG_SLOT_ABCDEF       = 6,                     /*!< PPG timeslot sequency ABCDEF */
    API_ADPD6000_PPG_SLOT_ABCDEFG      = 7,                     /*!< PPG timeslot sequency ABCDEFG */
    API_ADPD6000_PPG_SLOT_ABCDEFGH     = 8,                     /*!< PPG timeslot sequency ABCDEFGH */
    API_ADPD6000_PPG_SLOT_ABCDEFGHI    = 9,                     /*!< PPG timeslot sequency ABCDEFGHI */
    API_ADPD6000_PPG_SLOT_ABCDEFGHIJ   = 10,                    /*!< PPG timeslot sequency ABCDEFGHIJ */
    API_ADPD6000_PPG_SLOT_ABCDEFGHIJK  = 11,                    /*!< PPG timeslot sequency ABCDEFGHIJK */
    API_ADPD6000_PPG_SLOT_ABCDEFGHIJKL = 12,                    /*!< PPG timeslot sequency ABCDEFGHIJKL */
} adi_adpd6000_ppg_slot_mode_e;

/*!
 * @brief  PPG input mux enumuration, Odd for input A, Even for input B
*/
typedef enum {
    API_ADPD6000_PPG_INPUT_DISABLE = 0,                         /*!< Input pair disabled. None to channel 1. None to channel 2 */
    API_ADPD6000_PPG_INPUT_A1      = 1,                         /*!< Odd input to channel 1. None to channel 2. */
    API_ADPD6000_PPG_INPUT_A2      = 2,                         /*!< None to channel 1. Odd input to channel 2. */
    API_ADPD6000_PPG_INPUT_B1      = 3,                         /*!< Even input to channel 1. None to channel 2.*/
    API_ADPD6000_PPG_INPUT_B2      = 4,                         /*!< None to channel 1. Even input to channel 2. */
    API_ADPD6000_PPG_INPUT_A1_B2   = 5,                         /*!< Odd input to channel 1. Even input to channel 2. */
    API_ADPD6000_PPG_INPUT_A2_B1   = 6,                         /*!< Even input to channel 1. Odd input to channel 2. */
    API_ADPD6000_PPG_INPUT_A1_B1   = 7,                         /*!< Even and odd to channel 1. Single ended or differentially based on pair12. None to channel 2. */
    API_ADPD6000_PPG_INPUT_A2_B2   = 8,                         /*!< None to channel 1. Even and odd to channel 2. Single ended or differentially based on pair12. */
} adi_adpd6000_ppg_input_mux_e;

/*!
 * @brief  PPG input mux enumuration during sleep
*/
typedef enum {
    API_ADPD6000_PPG_INPUT_SLEEP_BOTH_FLOAT           = 0,       /*!< Both inputs float */
    API_ADPD6000_PPG_INPUT_SLEEP_SHORT                = 1,       /*!< Even and odd shorted together (floating short of differential) Only shorts if pair12 is 1. */
    API_ADPD6000_PPG_INPUT_SLEEP_BOTH_CATH1           = 2,       /*!< Both connected to Cathode1 (also shorted together if configured as differential pair). */
    API_ADPD6000_PPG_INPUT_SLEEP_BOTH_CATH2           = 3,       /*!< Both connected to Cathode2 (also shorted together if configured as differential pair). */
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_CATH1_EVEN_FLOAT = 4,       /*!< Odd connected to Cathode1. Even floating. */
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_CATH1_EVEN_CATH2 = 5,       /*!< Odd connected to Cathode1. Even connected to Cathode2. */
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_CATH2_EVEN_FLOAT = 6,       /*!< Odd connected to Cathode2. Even floating.*/
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_CATH2_EVEN_CATH1 = 7,       /*!< Odd connected to Cathode2. Even connected to Cathode1. */
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_FLOAT_EVEN_CATH1 = 8,       /*!< Odd floating. Even connected to Cathode1. */
    API_ADPD6000_PPG_INPUT_SLEEP_ODD_FLOAT_EVEN_CATH2 = 9,       /*!< Odd floating. Even connected to Cathode2. */
} adi_adpd6000_ppg_sleep_input_mux_e;

/*!
 * @brief  PPG cathode type enumuration during sleep
*/
typedef enum {
    API_ADPD6000_PPG_CATH_VDD    = 0,                            /*!< Cathode set to VDD during sleep. */
    API_ADPD6000_PPG_CATH_GND    = 1,                            /*!< Cathode set to GND during sleep. */
    API_ADPD6000_PPG_CATH_FLOAT  = 2,                            /*!< Cathode floating during sleep. */
} adi_adpd6000_ppg_cath_type_e;

/*!
 * @brief  PPG modulation type enumuration
*/
typedef enum {
    API_ADPD6000_PPG_MOD_TYPE_NONE     = 0,                     /*!< TIA continuously connected to input after precondition. No connection modulation. */
    API_ADPD6000_PPG_MOD_TYPE_FLOAT    = 1,                     /*!< Float type operation. Pulse connection from input to TIA with modulate pulse, floating between pulses. */
    API_ADPD6000_PPG_MOD_TYPE_NONFLOAT = 2,                     /*!< Non-float type connection modulation. Pulse connection from input to TIA. Connect to precondition value between pulses. */
} adi_adpd6000_ppg_mod_type_e;

/*!
 * @brief  PPG TIA input resistor enumuration
*/
typedef enum {
    API_ADPD6000_PPG_TIA_INPUT_RES_ESD = 0,                     /*!< ECD resistor only */
    API_ADPD6000_PPG_TIA_INPUT_RES_6K5 = 1,                     /*!< 6.5K resistor */
} adi_adpd6000_ppg_tia_input_res_e;

/*!
 * @brief  PPG TIA pulse value enumuration
*/
typedef enum {
    API_ADPD6000_PPG_TIA_VREF_1P1385 = 0,                       /*!< TIA vref is 1.1385V */
    API_ADPD6000_PPG_TIA_VREF_1P012  = 1,                       /*!< TIA vref is 1.012V */
    API_ADPD6000_PPG_TIA_VREF_0P8855 = 2,                       /*!< TIA vref is 0.8855V */
    API_ADPD6000_PPG_TIA_VREF_1P265  = 3,                       /*!< TIA vref is 1.265V */
} adi_adpd6000_ppg_tia_vref_value_e;

/*!
 * @brief  PPG TIA gain resistor value enumuration
*/
typedef enum {
    API_ADPD6000_PPG_TIA_GAIN_RES_400K = 0,                     /*!< TIA gain resistor is 400K Ohm */
    API_ADPD6000_PPG_TIA_GAIN_RES_200K = 1,                     /*!< TIA gain resistor is 200K Ohm */
    API_ADPD6000_PPG_TIA_GAIN_RES_100K = 2,                     /*!< TIA gain resistor is 100K Ohm */
    API_ADPD6000_PPG_TIA_GAIN_RES_50K  = 3,                     /*!< TIA gain resistor is 50K Ohm */
    API_ADPD6000_PPG_TIA_GAIN_RES_25K  = 4,                     /*!< TIA gain resistor is 25K Ohm */
    API_ADPD6000_PPG_TIA_GAIN_RES_12K5 = 5,                     /*!< TIA gain resistor is 12.5K Ohm */
} adi_adpd6000_ppg_tia_gain_res_e;

/*!
 * @brief  PPG Integrator input gain resistor value enumuration
*/
typedef enum {
    API_ADPD6000_PPG_INTEG_400K_GAIN_1 = 0,                     /*!< Rin = 400K or Buffer gain = 1 (Rfb/Rin = 200K/200K) */
    API_ADPD6000_PPG_INTEG_200K_GAIN_2 = 1,                     /*!< Rin = 200K or Buffer gain = 2 (Rfb/Rin = 200K/100K) */
    API_ADPD6000_PPG_INTEG_100K_GAIN_1 = 2,                     /*!< Rin = 100K or Buffer gain = 1 (Rfb/Rin = 100K/100K) */
    API_ADPD6000_PPG_INTEG_50K_GAIN_2  = 3,                     /*!< Rin = 50K or Buffer gain = 2 (Rfb/Rin = 100K/500K) */
} adi_adpd6000_ppg_integ_gain_res_e;

/*!
 * @brief  PPG Integrator gain cap value enumuration
*/
typedef enum {
    API_ADPD6000_PPG_INTEG_CAP_6P3  = 0,                        /*!< Cap is 6.3pF */
    API_ADPD6000_PPG_INTEG_CAP_12P6 = 1,                        /*!< Cap is 12.6pF */
} adi_adpd6000_ppg_integ_cap_e;

/*!
 * @brief  PPG LED channel enumuration
*/
typedef enum {
    API_ADPD6000_PPG_LED_A = 0,                                 /*!< Driver LED on output A */
    API_ADPD6000_PPG_LED_B = 1,                                 /*!< Driver LED on output B */
} adi_adpd6000_ppg_led_channel_e;

/*!
 * @brief  PPG LED mode enumuration
*/
typedef enum {
    API_ADPD6000_PPG_LED_HIGH_SNR = 0,                          /*!< High SNR mode */
    API_ADPD6000_PPG_LED_LOW_COMP = 1,                          /*!< Low compliance mode */
} adi_adpd6000_ppg_led_mode_e;

/*!
 * @brief  VCx selection enumuration
*/
typedef enum {
    API_ADPD6000_PPG_VC_VDD    = 0,                             /*!< VDD */
    API_ADPD6000_PPG_VC_VREF   = 1,                             /*!< AFE_Vref */
    API_ADPD6000_PPG_VC_DELTA  = 2,                             /*!< V_Delta */
    API_ADPD6000_PPG_VC_GND    = 3,                             /*!< GND */
} adi_adpd6000_ppg_vc_select_e;

/*!
 * @brief  VCx pulse enumuration
*/
typedef enum {
    API_ADPD6000_PPG_VC_PULSE_NO         = 0,                   /*!< No pulse */
    API_ADPD6000_PPG_VC_PULSE_ALT        = 1,                   /*!< Alternate odd/even timeslots */
    API_ADPD6000_PPG_VC_PULSE_MODULATED  = 2,                   /*!< Pulse to alternate value using Modulate pulse */
    API_ADPD6000_PPG_VC_PULSE_FLOATING   = 3,                   /*!< Leave VC1 floating */
} adi_adpd6000_ppg_vc_pulse_e;

/*!
 * @brief  Precondition value for enabled inputs enumuration
*/
typedef enum {
    API_ADPD6000_PPG_PRECON_FLOAT           = 0,                /*!< No pulse */
    API_ADPD6000_PPG_PRECON_VC1             = 1,                /*!< Alternate odd/even timeslots */
    API_ADPD6000_PPG_PRECON_VC2             = 2,                /*!< Pulse to alternate value using Modulate pulse */
    API_ADPD6000_PPG_PRECON_VICM            = 3,                /*!< Leave VC1 floating */
    API_ADPD6000_PPG_PRECON_TIA_INPUT       = 4,                /*!< Leave VC1 floating */
    API_ADPD6000_PPG_PRECON_AFE_VREF        = 5,                /*!< Leave VC1 floating */
    API_ADPD6000_PPG_PRECON_SHORT_DIFF_PAIR = 6,                /*!< Leave VC1 floating */
} adi_adpd6000_ppg_precon_e;

/*!
 * @brief  AFE datapath enumuration
*/
typedef enum {
    API_ADPD6000_PPG_AFE_PATH_TIA_BUF_ADC_2X  = 0x20,           /*!< TIA + INT/BUF + ADC(2x TIA Gain) */
    API_ADPD6000_PPG_AFE_PATH_TIA_BUF_ADC_1X  = 0x28,           /*!< TIA + BUF + ADC(1x TIA Gain) */
    API_ADPD6000_PPG_AFE_PATH_TIA_INT_ADC_1X  = 0x31,           /*!< TIA + INT + ADC(1x TIA Gain) */
    API_ADPD6000_PPG_AFE_PATH_INT_ADC         = 0x35,           /*!< INT + ADC */
    API_ADPD6000_PPG_AFE_PATH_ADC             = 0x41,           /*!< ADC */
} adi_adpd6000_ppg_afe_path_e;

/*!
 * @brief  PPG sample type enumuration
*/
typedef enum {
    API_ADPD6000_PPG_SAMPLE_TYPE_NORMAL      = 0,               /*!< Normal sampling mode */
    API_ADPD6000_PPG_SAMPLE_TYPE_TWO_PHASE   = 1,               /*!< Two phase normal sampling mode */
    API_ADPD6000_PPG_SAMPLE_TYPE_ONE_REGION  = 2,               /*!< One region digital mode */
    API_ADPD6000_PPG_SAMPLE_TYPE_TWO_REGION  = 3,               /*!< Two region digital mode */
    API_ADPD6000_PPG_SAMPLE_TYPE_IMPULSE     = 4,               /*!< Impulse response mode */
} adi_adpd6000_ppg_sample_type_e;

/*!
 * @brief  Ambient cancellation DAC Control type enumuration
*/
typedef enum {
    API_ADPD6000_PPG_ALC_DISABLE     = 0,                       /*!< Disable the ambient cancellation loop */
    API_ADPD6000_PPG_ALC_COARSE_FINE = 1,                       /*!< Enable coarse + fine loop */
    API_ADPD6000_PPG_ALC_COARSE      = 2,                       /*!< Enable coarse loop only */
    API_ADPD6000_PPG_ALC_MCU         = 3,                       /*!< Enable MCU control */
} adi_adpd6000_ppg_alc_type_e;

/*!
 * @brief  The time duration for the coarse ambient cancellation loop enumuration
*/
typedef enum {
    API_ADPD6000_PPG_ALC_LOOP_WIDTH_10 = 0,                     /*!< 10 */
    API_ADPD6000_PPG_ALC_LOOP_WIDTH_20 = 1,                     /*!< 20 */
} adi_adpd6000_ppg_alc_loop_width_e;

/*!
 * @brief  PPG slot AGC configuration structure
*/
typedef struct
{
    uint8_t  agc_en : 1;                                         /*!< 0 - disable, 1 - enable */
    uint8_t  led_chnl : 1;                                       /*!< LED 0 or 1 for AGC */
    uint8_t  led_ab : 1;                                         /*!< LED A or B for AGC, 0 for A, 1 for B */
    uint8_t  tia_chnl : 1;                                       /*!< 0 - use tia channel 1, 1 - use tia channel 2 */
    uint8_t  agc_type : 1;                                       /*!< 0 - continuous, 1 - one shot */
} adi_adpd6000_ppg_agc_slot_t;

/*!
 * @brief  PPG AGC configuration structure
*/
typedef struct
{
    uint16_t ppg_skip_sample_number;                             /*!< skip the first few data for process */
    uint16_t ppg_average_sample_number;                          /*!< average sample count */
    uint8_t  power_first_en;                                     /* 0 - use low TIA gain, 1 - use low LED current */
    adi_adpd6000_ppg_agc_slot_t slot[12];                  
} adi_adpd6000_ppg_agc_cfg_t;

/*!
 * @brief  PPG AGC running data structure
*/
typedef struct
{    
    uint32_t ppg_full_scale;  
    uint32_t ppg_data_sum;
    uint8_t  tia_gain;
    uint8_t  led_current;
    uint8_t  agc_done;
} adi_adpd6000_ppg_agc_run_t;

/*!
 * @brief  adpd6000 bioz fifo data
 */
typedef struct
{
    uint32_t real;                                                  /* I */
    uint32_t imag;                                                  /* Q */
} adi_adpd6000_bioz_slot_data_t;

/*!
 * @brief  BioZ slot mode enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_SLOT_NONE         = 0,                    /*!< No BioZ timeslot */
    API_ADPD6000_BIOZ_SLOT_A            = 1,                    /*!< BioZ timeslot sequency A only */
    API_ADPD6000_BIOZ_SLOT_AB           = 2,                    /*!< BioZ timeslot sequency AB */
    API_ADPD6000_BIOZ_SLOT_ABC          = 3,                    /*!< BioZ timeslot sequency ABC */
    API_ADPD6000_BIOZ_SLOT_ABCD         = 4,                    /*!< BioZ timeslot sequency ABCD */
    API_ADPD6000_BIOZ_SLOT_ABCDE        = 5,                    /*!< BioZ timeslot sequency ABCDE */
    API_ADPD6000_BIOZ_SLOT_ABCDEF       = 6,                    /*!< BioZ timeslot sequency ABCDEF */
} adi_adpd6000_bioz_slot_mode_e;

/*!
 * @brief  BioZ TIA Gain Res enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_TIA_GAIN_1K       = 0,                    /*!< Gain resistor is 1K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_2K       = 1,                    /*!< Gain resistor is 2K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_3K       = 2,                    /*!< Gain resistor is 3K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_4K       = 3,                    /*!< Gain resistor is 4K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_6K       = 4,                    /*!< Gain resistor is 6K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_8K       = 5,                    /*!< Gain resistor is 8K Ohm */
    API_ADPD6000_BIOZ_TIA_GAIN_10K      = 6,                    /*!< Gain resistor is 10K Ohm */
} adi_adpd6000_bioz_tia_gain_res_e;

/*!
 * @brief  BioZ SINC3 oversample rate enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_SINC3_OVERSAMPLE_RATE_5       = 0,        /*!< Oversampling rate is 5 */
    API_ADPD6000_BIOZ_SINC3_OVERSAMPLE_RATE_4       = 1,        /*!< Oversampling rate is 4 */
    API_ADPD6000_BIOZ_SINC3_OVERSAMPLE_RATE_2       = 2,        /*!< Oversampling rate is 2 */
    API_ADPD6000_BIOZ_SINC3_OVERSAMPLE_RATE_5_1     = 3,        /*!< Oversampling rate is 5 */
} adi_adpd6000_bioz_sinc3_os_rate_e;

/*!
 * @brief  BioZ SINC3 average samples enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_SINC3_AVERAGE_2       = 0,                /*!< 2 ADC samples */
    API_ADPD6000_BIOZ_SINC3_AVERAGE_4       = 1,                /*!< 4 ADC samples */
    API_ADPD6000_BIOZ_SINC3_AVERAGE_8       = 2,                /*!< 8 ADC samples */
    API_ADPD6000_BIOZ_SINC3_AVERAGE_16      = 3,                /*!< 16 ADC samples */
} adi_adpd6000_bioz_sinc3_avg_e;

/*!
 * @brief  BioZ DFT point number enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_DFT_POINT_4         = 0,                  /*!< DFT point number is 4 */
    API_ADPD6000_BIOZ_DFT_POINT_8         = 1,                  /*!< DFT point number is 8 */
    API_ADPD6000_BIOZ_DFT_POINT_16        = 2,                  /*!< DFT point number is 16 */
    API_ADPD6000_BIOZ_DFT_POINT_32        = 3,                  /*!< DFT point number is 32 */
    API_ADPD6000_BIOZ_DFT_POINT_64        = 4,                  /*!< DFT point number is 64 */
    API_ADPD6000_BIOZ_DFT_POINT_128       = 5,                  /*!< DFT point number is 128 */
    API_ADPD6000_BIOZ_DFT_POINT_256       = 6,                  /*!< DFT point number is 256 */
    API_ADPD6000_BIOZ_DFT_POINT_512       = 7,                  /*!< DFT point number is 512 */
    API_ADPD6000_BIOZ_DFT_POINT_1024      = 8,                  /*!< DFT point number is 1024 */
    API_ADPD6000_BIOZ_DFT_POINT_2048      = 9,                  /*!< DFT point number is 2048 */
    API_ADPD6000_BIOZ_DFT_POINT_4096      = 10,                 /*!< DFT point number is 4096 */
    API_ADPD6000_BIOZ_DFT_POINT_8192      = 11,                 /*!< DFT point number is 8192 */
} adi_adpd6000_bioz_dft_point_e;

/*!
 * @brief  BioZ AMP mode enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_AMP_MODE_ENABLE_ALL   = 0,                /*!< 2 ADC samples */
    API_ADPD6000_BIOZ_AMP_MODE_ENABLE_P     = 1,                /*!< 4 ADC samples */
    API_ADPD6000_BIOZ_AMP_MODE_ENABLE_M     = 2,                /*!< 8 ADC samples */
    API_ADPD6000_BIOZ_AMP_MODE_ENABLE_P_M   = 3,                /*!< 16 ADC samples */
} adi_adpd6000_bioz_amp_mode_e;

/*!
 * @brief  BioZ connection enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_IMPIP = 1,                                /*!< Connect to biz-impedance to IMPIP */
    API_ADPD6000_BIOZ_IMPIN = 2,                                /*!< Connect to biz-impedance to IMPIN */
    API_ADPD6000_BIOZ_EXCP  = 4,                                /*!< Connect to biz-impedance to EXCP */
    API_ADPD6000_BIOZ_EXCN  = 8,                                /*!< Connect to biz-impedance to EXCN */
} adi_adpd6000_bioz_imp_pin_e;

/*!
 * @brief  BioZ connection enumuration
*/
typedef enum {
    API_ADPD6000_BIOZ_CONN_EXT_RCAL_VOL = 0,                    /*!< Connect to external RCal, and get RCal voltage data */
    API_ADPD6000_BIOZ_CONN_EXT_RCAL_CUR = 1,                    /*!< Connect to external RCal, and get RCal current data */
    API_ADPD6000_BIOZ_CONN_INT_RCAL_VOL = 2,                    /*!< Connect to internal RCal, and get RCal voltage data */
    API_ADPD6000_BIOZ_CONN_INT_RCAL_CUR = 3,                    /*!< Connect to internal RCal, and get RCal current data */
    API_ADPD6000_BIOZ_CONN_BIO_IMP_VOL  = 4,                    /*!< Connect to biz-impedance, and get voltage data */
    API_ADPD6000_BIOZ_CONN_BIO_IMP_CUR  = 5,                    /*!< Connect to biz-impedance, and get current data */
} adi_adpd6000_bioz_slot_connect_e;

/*!
 * @brief  ECG ODR value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_ODR_250  = 0,                              /*!< Output data rate is 250Hz */
    API_ADPD6000_ECG_ODR_500  = 1,                              /*!< Output data rate is 500Hz */
    API_ADPD6000_ECG_ODR_1000 = 2,                              /*!< Output data rate is 1KHz */
    API_ADPD6000_ECG_ODR_2000 = 3,                              /*!< Output data rate is 2KHz */
    API_ADPD6000_ECG_ODR_4000 = 4,                              /*!< Output data rate is 4KHz */
} adi_adpd6000_ecg_odr_e;

/*!
 * @brief  ECG RLD(right leg drive) output value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_RLD_OUTPUT_GND      = 0,                   /*!< AGND */
    API_ADPD6000_ECG_RLD_OUTPUT_AVDD_3V  = 1,                   /*!< AVDD3V */
    API_ADPD6000_ECG_RLD_OUTPUT_AVDD_1V5 = 2,                   /*!< AVDD3V/2 */
    API_ADPD6000_ECG_RLD_OUTPUT_CM_INPUT = 3,                   /*!< Regulated commom-mode input */
} adi_adpd6000_ecg_rld_output_e;

/*!
 * @brief  ECG DC lead off threshold value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_DC_LEADOFF_100MV  = 0,                     /*!< High threshold=AVDD3V-0.1V;  low threshold=0.1V. */
    API_ADPD6000_ECG_DC_LEADOFF_200MV  = 1,                     /*!< High threshold=AVDD3V-0.2V;  low threshold=0.2V. */
    API_ADPD6000_ECG_DC_LEADOFF_300MV  = 2,                     /*!< High threshold=AVDD3V-0.3V;  low threshold=0.3V. */
    API_ADPD6000_ECG_DC_LEADOFF_400MV  = 3,                     /*!< High threshold=AVDD3V-0.4V;  low threshold=0.4V. */
} adi_adpd6000_ecg_dc_leadoff_th_e;

/*!
 * @brief  ECG RLD AC lead off current magnitude value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_AC_LEADOFF_0NA   = 0,                      /*!< 0nA */
    API_ADPD6000_ECG_AC_LEADOFF_10NA  = 1,                      /*!< 10nA */
    API_ADPD6000_ECG_AC_LEADOFF_20NA  = 2,                      /*!< 20nA */
    API_ADPD6000_ECG_AC_LEADOFF_30NA  = 3,                      /*!< 30nA */
    API_ADPD6000_ECG_AC_LEADOFF_40NA  = 4,                      /*!< 40nA */
    API_ADPD6000_ECG_AC_LEADOFF_50NA  = 5,                      /*!< 50nA */
    API_ADPD6000_ECG_AC_LEADOFF_60NA  = 6,                      /*!< 60nA */
    API_ADPD6000_ECG_AC_LEADOFF_70NA  = 7,                      /*!< 70nA */
} adi_adpd6000_ecg_ac_leadoff_mag_e;

/*!
 * @brief  ECG RLD DC lead off current magnitude value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_DC_LEADOFF_0NA   = 0,                      /*!< 0nA */
    API_ADPD6000_ECG_DC_LEADOFF_2NA   = 8,                      /*!< 2nA */
    API_ADPD6000_ECG_DC_LEADOFF_4NA   = 9,                      /*!< 4nA */
    API_ADPD6000_ECG_DC_LEADOFF_6NA   = 10,                     /*!< 6nA */
    API_ADPD6000_ECG_DC_LEADOFF_8NA   = 11,                     /*!< 8nA */
    API_ADPD6000_ECG_DC_LEADOFF_10NA  = 12,                     /*!< 10nA */
    API_ADPD6000_ECG_DC_LEADOFF_12NA  = 13,                     /*!< 12nA */
    API_ADPD6000_ECG_DC_LEADOFF_14NA  = 14,                     /*!< 14nA */
    API_ADPD6000_ECG_DC_LEADOFF_16NA  = 15,                     /*!< 16nA */
} adi_adpd6000_ecg_dc_leadoff_mag_e;

/*!
 * @brief  ECG RLD DC lead off current polarity value enumuration
*/
typedef enum {
    API_ADPD6000_ECG_DC_LEADOFF_CURRENT_SINK    = 0,            /*!< Sink current */
    API_ADPD6000_ECG_DC_LEADOFF_CURRENT_SOURCE  = 1,            /*!< Sink current */
} adi_adpd6000_ecg_dc_leadoff_pol_e;

/*!
 * @brief  Interrupt type enumuration
*/
typedef enum {
    API_ADPD6000_INTERRUPT_X    = 0,                           /*!< Interrupt X */
    API_ADPD6000_INTERRUPT_Y    = 1,                           /*!< Interrupt Y */
} adi_adpd6000_interrupt_type_e;

/*!
 * @brief  System clock source enumuration
*/
typedef enum {
    API_ADPD6000_SYS_CLOCK_INT_CLK   = 0,                    /*!< Internal clocks used */
    API_ADPD6000_SYS_CLOCK_EXT_LF    = 1,                    /*!< Use gpio for lf osc, 960KHz. Timer clock (tm_clk) will also use this as source */
    API_ADPD6000_SYS_CLOCK_EXT_HF    = 2,                    /*!< Use gpio for hf osc, 32MHz. */
    API_ADPD6000_SYS_CLOCK_EXT_BOTH  = 3,                    /*!< Use gpio for hf osc (32MHz), generate lf (1MHz) from hf. This should only be used when ECG is disabled */
    API_ADPD6000_SYS_CLOCK_EXT_TM    = 4,                    /*!< Use gpio for timer clock, 32KHz or 960KHz */
    
} adi_adpd6000_sys_clock_src_e;

#ifdef __cplusplus
extern "C" {
#endif

/*============= E X P O R T S ==============*/
/**
 * @brief  HAL register read function.
 *         
 * @param  device     Pointer to device structure
 * @param  reg_addr   Register address to read
 * @param  reg_data   Pointer to save readback data
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_reg_read(adi_adpd6000_device_t *device, uint32_t reg_addr, uint16_t *reg_data);

/**
 * @brief  HAL register write function.
 *         
 * @param  device     Pointer to device structure
 * @param  reg_addr   Register address to write
 * @param  reg_data   Register value to write
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_reg_write(adi_adpd6000_device_t *device, uint32_t reg_addr, uint16_t reg_data);

/**
 * @brief  HAL bit field read function.
 *         
 * @param  device     Pointer to device structure
 * @param  reg_addr   Register address to read
 * @param  bf_info    Bit field information, byte0 - start bit, byte1 - bits length
 * @param  bf_val     Pointer to bit field read back value
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_bf_read(adi_adpd6000_device_t *device, uint32_t reg_addr, uint32_t bf_info, uint16_t *bf_val);

/**
 * @brief  HAL bit field read function.
 *         
 * @param  device     Pointer to device structure
 * @param  reg_addr   Register address to read
 * @param  bf_info    Bit field information, byte0 - start bit, byte1 - bits length
 * @param  bf_val     Pointer to bit field read back value
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_bf_write(adi_adpd6000_device_t *device, uint32_t reg_addr, uint32_t bf_info, uint16_t bf_val);

/**
 * @brief  HAL error report function.
 *         
 * @param  device     Pointer to device structure
 * @param  log_type   Log type (LARK_LOG_NONE/MSG/WARN/ERROR/ALL)
 * @param  file_name  Source file name where error happens
 * @param  line_num   Source file line number where error happens
 * @param  func_name  Function name where error happens
 * @param  var_name   Variable name where error happens
 * @param  comment    Error message
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_error_report(adi_adpd6000_device_t* device, uint32_t log_type,
    const char* file_name, const char* func_name, uint32_t line_num, const char* var_name, const char* comment);

/**
 * @brief  HAL log write function.
 *         
 * @param  device     Pointer to device structure
 * @param  log_type   Log type (LARK_LOG_NONE/MSG/WARN/ERROR/ALL)
 * @param  comment    Log message format
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_log_write(adi_adpd6000_device_t *device, uint32_t log_type, const char* comment, ...);

/**
 * @brief  HAL FIFO read function.
 *         
 * @param  device     Pointer to device structure
 * @param  reg_addr   Register address to read
 * @param  reg_data   Pointer to save readback data
 * @param  len        Data len
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_hal_fifo_read_bytes(adi_adpd6000_device_t *device, uint32_t reg_addr, uint8_t *reg_data, uint32_t len);

/**
 * @brief  Get device id and device revision
 *         
 * @param  device     Pointer to device structure
 * @param  chip_id    Pointer to chip id value
 * @param  chip_rev   Pointer to device revision value
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_get_id(adi_adpd6000_device_t *device, uint8_t *chip_id, uint8_t *chip_rev);

/**
 * @brief  Get this sdk revision
 *         
 * @param  device     Pointer to device structure
 * @param  rev        Pointer to revision value (major.minor.build, one byte each)
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_lark_error_e
 */
int32_t adi_adpd6000_device_get_sdk_rev(adi_adpd6000_device_t *device, uint32_t *rev);

/**
 * @brief  Reset this device in soft way
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_sw_reset(adi_adpd6000_device_t *device);

/**
 * @brief  Iint device, including trim, calibration...
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_init(adi_adpd6000_device_t *device);

/**
 * @brief  Enable slot operation mode
 *         
 * @param  device     Pointer to device structure
 * @param  enable     true - Go mode, false - standby mode
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_enable_slot_operation_mode_go(adi_adpd6000_device_t *device, bool enable);

/**
 * @brief  Get FIFO data size
 *         
 * @param  device     Pointer to device structure
 * @param  count      Pointer to fifo count
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_get_fifo_count(adi_adpd6000_device_t *device, uint16_t *count);

/**
 * @brief  Clear FIFO data
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_clr_fifo(adi_adpd6000_device_t *device);

/**
 * @brief  Set FIFO threshold
 *         
 * @param  device     Pointer to device structure
 * @param  threshold  Threshold
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_set_fifo_threshold(adi_adpd6000_device_t *device, uint16_t threshold);

/**
 * @brief  Clear FIFO
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_clr_fifo(adi_adpd6000_device_t *device);

/**
 * @brief  Get FIFO count
 *         
 * @param  device     Pointer to device structure
 * @param  count      FIFO count
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_get_fifo_count(adi_adpd6000_device_t *device, uint16_t *count);

/**
 * @brief  Read data from FIFO
 *         
 * @param  device     Pointer to device structure
 * @param  data       Pointer to fifo data
 * @param  len        Data size
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_fifo_read_bytes(adi_adpd6000_device_t *device, uint8_t *data, uint32_t len);

/**
 * @brief  Get FIFO interrupt status
 *         
 * @param  device     Pointer to device structure
 * @param  status     Pointer to interrupt status, bit0 - threshold int, bit1 - overflow int, bit2 - underflow
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_fifo_get_fifo_int_status(adi_adpd6000_device_t *device, uint8_t *status);

/**
 * @brief  Enable/disable FIFO threshold interrupt
 *         
 * @param  device     Pointer to device structure
 * @param  type       @see adi_adpd6000_interrupt_type_e
 * @param  enable     false - disable fifo threshold interrput, true - enable fifo threshold interrput
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_enable_fifo_thres_interrupt(adi_adpd6000_device_t *device, adi_adpd6000_interrupt_type_e type, bool enable);

/**
 * @brief  Enable/disable automatic clearing of the FIFO threshold interrupt each time the FIFO is read.
 *         
 * @param  device     Pointer to device structure
 * @param  enable     false - disable, true - enable
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_enable_auto_clear_int(adi_adpd6000_device_t *device, bool enable);

/**
 * @brief  Get sequence fifo configuration
 * @param  device     Pointer to device structure
 * @param  fifo       @see adi_adpd6000_fifo_config_t
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_get_sequence_fifo_config(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo);

/**
 * @brief  Enable internal low frequency OSC
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_enable_internal_osc_960k(adi_adpd6000_device_t *device);

/**
 * @brief  Set high/low frequency OSC
 *         
 * @param  device     Pointer to device structure
 * @param  high_osc   High OSC trim
 * @param  low_osc    Low OSC trim
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_set_osc_trim(adi_adpd6000_device_t *device, uint16_t high_osc, uint16_t low_osc);

/**
 * @brief  Config external system clock source
 *         
 * @param  device     Pointer to device structure
 * @param  src        @see adi_adpd6000_sys_clock_src_e
 * @param  clk_pin    gpio index, 0~2, gpiox for alternate clock
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_config_ext_clock(adi_adpd6000_device_t *device, adi_adpd6000_sys_clock_src_e src, uint8_t clk_pin);

/**
 * @brief  Set timeslot frequency
 *         
 * @param  device     Pointer to device structure
 * @param  sys_clk    System clock
 * @param  freq       timeslot drequency, unit: Hz
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_set_slot_freq(adi_adpd6000_device_t *device, uint32_t sys_clk, uint32_t freq);

/**
 * @brief  Enable/disable sleep mode, if sleep mode, chip is in sleep before first timeslot sequence on GO mode
 *         
 * @param  device     Pointer to device structure
 * @param  enable     true - sleep mode
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_enbale_sleep_mode(adi_adpd6000_device_t *device, bool enable);

/**
 * @brief  Read low osc trim code from efuse and write it to 960k osc trim register
 *         
 * @param  device     Pointer to device structure
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_device_cal_960k_osc(adi_adpd6000_device_t *device);

/**
 * @brief  Enable/disable ECG timeslot
 *         
 * @param  device     Pointer to device structure
 * @param  enable     false - disable, true - enable
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_enable_slot(adi_adpd6000_device_t *device, bool enable);

/**
 * @brief  Select inputs of ECG, if short_en is true, internally short the inputs of ECG. The feature can be used for ECG noise measurements.
 *         
 * @param  device     Pointer to device structure
 * @param  input_en   false - disconnect, true - connect to the ECG inputs
 * @param  short_en   false - disconnect, true - internally short the inputs of ECG
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_set_input_mux(adi_adpd6000_device_t *device, bool input_en, bool short_en);

/**
 * @brief  Set ECG output data rate, 250Hz: 500Hz, 1KHz, 2KHz, 4KHz.
 *         
 * @param  device     Pointer to device structure
 * @param  odr        @see adi_adpd6000_ecg_odr_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_set_odr(adi_adpd6000_device_t *device, adi_adpd6000_ecg_odr_e odr);

/**
 * @brief  Set ECG over sample rate, The ratio of ECG ODR to PPG ODR. This field must be configured as oversample = 
 *         ecg_odr_sel/(960KHz/timeslot_period) if using internal lfosc or ratio = ecg_odr_sel/(external sync rate) 
 *         if using external triggers. Result should be rounded up to the nearest integer.
 *         
 * @param  device     Pointer to device structure
 * @param  oversample over sample ratio
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_set_oversample(adi_adpd6000_device_t *device, uint8_t oversample);

/**
 * @brief  Enable/disable ECG stsus byte in FIFO output, status byte | ECG byte | ECG byte | ECG byte.
 *         
 * @param  device     Pointer to device structure
 * @param  enable     false - output data doesn't include status byte, true - output data includes status byte
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_enable_statusbyte(adi_adpd6000_device_t *device, bool enable);

/**
 * @brief  Get ECG status, for detailed bitfield information, please check datasheet.
 *         
 * @param  device     Pointer to device structure
 * @param  status     Pointer to status
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_get_ecg_status(adi_adpd6000_device_t *device, uint8_t *status);

/**
 * @brief  Clear status, for detailed bitfield information, please check datasheet.
 *         
 * @param  device     Pointer to device structure
 * @param  status     Status
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_clr_ecg_status(adi_adpd6000_device_t *device, uint8_t status);

/**
 * @brief  Config RLD(right leg drive).
 *         
 * @param  device     Pointer to device structure
 * @param  enable     fasle - disable RLD loop, true - enable RLD loop
 * @param  rld        @see adi_adpd6000_ecg_rld_output_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_set_rld(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_rld_output_e rld);

/**
 * @brief  Enable/disable the DC lead off for the RLD pin and set threshold.
 *         
 * @param  device     Pointer to device structure
 * @param  enable     false - DC lead off for RLD is disabled., true - DC lead off for RLD is enabled.
 * @param  th         @see adi_adpd6000_ecg_dc_leadoff_th_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_leadoff_set_rld(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_dc_leadoff_th_e th);

/**
 * @brief  Set AC leadoff configuration.
 *         
 * @param  device     Pointer to device structure
 * @param  enable     Enable/disable the AC lead off detector for the ECG inputs..
 * @param  mag        AC lead off excite current magnitude.@see adi_adpd6000_ecg_ac_leadoff_mag_e
 * @param  th         AC lead off Threshold selection for the ECG inputs.
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_leadoff_set_ac(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_ac_leadoff_mag_e mag, uint8_t th);

/**
 * @brief  Set DC leadoff configuration.
 *         
 * @param  device     Pointer to device structure
 * @param  enable     Enable/disable the AC lead off detector for the ECG inputs..
 * @param  mag        AC lead off excite current magnitude.@see adi_adpd6000_ecg_ac_leadoff_mag_e
 * @param  th         @see adi_adpd6000_ecg_dc_leadoff_th_e.
 * @param   in_pol     DC lead off output current polarity at ECGIP. @see adi_adpd6000_ecg_dc_leadoff_pol_e
 * @param   ip_pol     DC lead off output current polarity at ECGIN. @see adi_adpd6000_ecg_dc_leadoff_pol_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_leadoff_set_dc(adi_adpd6000_device_t *device, bool enable, adi_adpd6000_ecg_dc_leadoff_mag_e mag,
    adi_adpd6000_ecg_dc_leadoff_th_e th, adi_adpd6000_ecg_dc_leadoff_pol_e in_pol, adi_adpd6000_ecg_dc_leadoff_pol_e ip_pol);

/**
 * @brief  Read raw ECG data from FIFO based on FIFO configuration, please call adi_adpd6000_device_get_sequence_fifo_config() before the function.
 *         
 * @param  device     Pointer to device structure
 * @param  fifo       @see adi_adpd6000_fifo_config_t
 * @param  ecg_data   Pointer to ECG data
 * @param  ecg_num    Pointer to ECG channel number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_read_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *ecg_data, uint8_t *ecg_num);

/**
 * @brief  Read ECG data and status from FIFO based on FIFO configuration, please call adi_adpd6000_device_get_sequence_fifo_config() before the function.
 *         
 * @param  device     Pointer to device structure
 * @param  fifo       @see adi_adpd6000_fifo_config_t
 * @param  ecg_data   Pointer to ECG data
 * @param  status     Pointer to status, if do not want get status, pass NULL
 * @param  ecg_num    Pointer to ECG channel number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ecg_read_data_status(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *ecg_data, uint8_t *status, uint8_t *ecg_num);

/**
 * @brief  Set PPG time slot mode.
 *         
 * @param  device     Pointer to device structure
 * @param  mode       @see adi_adpd6000_ppg_slot_mode_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_slot_mode(adi_adpd6000_device_t *device, adi_adpd6000_ppg_slot_mode_e mode);

/**
 * @brief  Get PPG aviable slot number
 *         
 * @param  device      Pointer to device structure
 * @param  slot_num    Pointer to slot number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_get_slot_num(adi_adpd6000_device_t *device, uint8_t *slot_num);

/**
 * @brief  Get PPG aviable channels number
 *         
 * @param  device      Pointer to device structure
 * @param  channel_num Pointer to channel number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_get_channel_num(adi_adpd6000_device_t *device, uint8_t *channel_num);

/**
 * @brief  Select input resistor, ESD resistor or 6.5K resistor.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  res             @see adi_adpd6000_ppg_tia_input_res_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_tia_set_input_res(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_input_res_e res);

/**
 * @brief  Set Tia resistor gain setting.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel, 0 or 1
 * @param  gain            @see adi_adpd6000_ppg_tia_gain_res_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_tia_set_gain_res(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_tia_gain_res_e gain);

/**
 * @brief  Set voltage trim for ref buffer.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  vref_value      @see adi_adpd6000_ppg_tia_vref_value_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_tia_set_vref_value(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_vref_value_e vref_value);

/**
 * @brief  Set Vref pulse alternate value.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  alt_value       @see adi_adpd6000_ppg_tia_vref_value_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_tia_set_vref_pulse_alt_value(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_tia_vref_value_e alt_value);

/**
 * @brief  Enable/disable pulse Vref.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  enable          false - No pulsing, true - Pulse Vref based on modulate pulse
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_tia_enable_vref_pulse(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

/**
 * @brief  Set input mode, single or diff mode.
 *         
 * @param  device          Pointer to device structure
 * @param  pair_index      Pair index, 0 for input 1 and 2, 1 for input 3 and 4
 * @param  diff_en         false - single ended inputs, true - diff ended inputs
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_enable_input_diff_mode(adi_adpd6000_device_t *device, uint8_t pair_index, bool diff_en);

/**
 * @brief  Set input mux.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  pair_index      Pair index, 0 for input 1 and 2, 1 for input 3 and 4
 * @param  mux             @see adi_adpd6000_ppg_input_mux_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_input_mux(adi_adpd6000_device_t *device, uint8_t slot, uint8_t pair_index, adi_adpd6000_ppg_input_mux_e mux);

/**
 * @brief  Set input input pairs during sleep and when the inputs are not active..
 *         
 * @param  device          Pointer to device structure
 * @param  pair_index      Pair index, 0 for input 1 and 2, 1 for input 3 and 4
 * @param  mux             @see adi_adpd6000_ppg_sleep_input_mux_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_sleep_input_mux(adi_adpd6000_device_t *device, uint8_t pair_index, adi_adpd6000_ppg_sleep_input_mux_e mux);


/**
 * @brief  Set Cathode2 sleep state.
 *         
 * @param  device          Pointer to device structure
 * @param  cath1           @see adi_adpd6000_ppg_cath_type_e
 * @param  cath2           @see adi_adpd6000_ppg_cath_type_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_cathode(adi_adpd6000_device_t *device, adi_adpd6000_ppg_cath_type_e cath1, adi_adpd6000_ppg_cath_type_e cath2);

/**
 * @brief  Config modulation.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  type            @see adi_adpd6000_ppg_mod_type_e
 * @param  width           Modulation pulse width
 * @param  offset          Modulation pulse offset
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_cfg_modulator(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_mod_type_e type,  uint8_t width, uint8_t offset);

/**
 * @brief  Enable/disable amplifier.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel, 0 or 1
 * @param  enable          false - disable amplifier, true - enable amplifier
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_enable_amp(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, bool enable);

/**
 * @brief  Set integrator resistor or buffer gain.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel, 0 or 1
 * @param  gain            @see adi_adpd6000_ppg_integ_gain_res_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_integ_set_gain(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_integ_gain_res_e gain);

/**
 * @brief  Set integrator capacitor.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel, 0 or 1
 * @param  cap             @see adi_adpd6000_ppg_integ_cap_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_integ_select_cap(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, adi_adpd6000_ppg_integ_cap_e cap);

/**
 * @brief  Set Integrator clock width.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  width           Integrator clock width
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_integ_set_width(adi_adpd6000_device_t *device, uint8_t slot, uint8_t width);

/**
 * @brief  Set Integrator clock offset.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  offset          Integrator clock offset
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_integ_set_offset(adi_adpd6000_device_t *device, uint8_t slot, uint16_t offset);

/**
 * @brief  Skip the second intergrator clock or not.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  enable          false - Use both generated integrator clocks, true - Skip the second integrator clock
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_integ_enable_single_clk(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

/**
 * @brief  Set current for LED1 or 2, Set to 0 to disable. Output current varies monotonically from 3ma to 200ma for values between 1 and 0x7F..
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  led_idx         LED index, 0 or 1
 * @param  current         current
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_current(adi_adpd6000_device_t *device, uint8_t slot, uint8_t led_idx, uint8_t current);

/**
 * @brief  Set LED output as A or B.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  led_idx         LED index, 0 or 1
 * @param  chl             @see adi_adpd6000_ppg_led_channel_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_channel(adi_adpd6000_device_t *device, uint8_t slot, uint8_t led_idx, adi_adpd6000_ppg_led_channel_e chl);

/**
 * @brief  Set LED operation mode, high SNR mode or low compliance mode.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  led_idx         LED index, 0 or 1
 * @param  mode            @see adi_adpd6000_ppg_led_mode_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_mode(adi_adpd6000_device_t *device, uint8_t slot, uint8_t led_idx, adi_adpd6000_ppg_led_mode_e mode);

/**
 * @brief  Set LED pulse width.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  width           width
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_width(adi_adpd6000_device_t *device, uint8_t slot, uint8_t width);

/**
 * @brief  Set LED pulse offset.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  offset          LED pulse offset
 * @param  sec_offset      LED pulse offset for the second LED phase
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_offset(adi_adpd6000_device_t *device, uint8_t slot, uint8_t offset, uint8_t sec_offset);

/**
 * @brief  Set LED pulse count.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  num_int         Number of integrates/adc or acquisition width. 0 not allowed. Number of analog integration cycles per adc conversion or the acquistion width for digital integration and impulse mod
 * @param  num_repeat      Number of sequence repeats. 0 not allowed. Total number of pulses = num_int * num_repeat.
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_led_set_count(adi_adpd6000_device_t *device, uint8_t slot, uint16_t num_int, uint16_t num_repeat);

/**
 * @brief  Set adjustment to ADC value.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  adjust1         This value is subtracted from the ADC value for channel 1
 * @param  adjust2         This value is subtracted from the ADC value for channel 2
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_adc_adjust(adi_adpd6000_device_t *device, uint8_t slot, uint16_t adjust1, uint16_t adjust2);
/**
 * @brief  Set Minimum Period for pulse repetition. Override for the automatically calculated period. 
 *         Used in Float type operations to set the float time of second and subsequent floats using the formula float=min_period-mod_width..
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  min_period      Minimum Period
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_minperiod(adi_adpd6000_device_t *device, uint8_t slot, uint16_t min_period);

/**
 * @brief  Set Precondition value for enabled inputs during this timeslot. 
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  sel             @see adi_adpd6000_ppg_precon_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_sel_precon(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_precon_e sel);

/**
 * @brief  Bypass and input mux select. Integrator will be either an integrator or buffer based on mode and afe_int_c_buf for the active timeslot. 
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  sel             @see adi_adpd6000_ppg_afe_path_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_sel_afe_path(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_afe_path_e sel);

/**
 * @brief  Config VC.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  vc_index        VC index, 0 or 1
 * @param  vc_sel          VC1 active state, @see adi_adpd6000_ppg_vc_select_e
 * @param  vc_alt          VC1 alternate pulsed state, @see adi_adpd6000_ppg_vc_select_e
 * @param  vc_pulse        VC1 pulse control, @see adi_adpd6000_ppg_vc_pulse_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_config_vc(adi_adpd6000_device_t *device, uint8_t slot, uint8_t vc_index, adi_adpd6000_ppg_vc_select_e vc_sel, 
    adi_adpd6000_ppg_vc_select_e vc_alt, adi_adpd6000_ppg_vc_pulse_e vc_pulse);

/**
 * @brief  Enable/disable channel2.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  enable          false - disable channels, true - enable channel2
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_enable_channe2(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

/**
 * @brief  Set decimate Sample Divider (minus 1).
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  enable          true - subsample using decimate_factor
 * @param  decimate        decimate value, minus value is 1
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_decimate(adi_adpd6000_device_t *device, uint8_t slot, bool enable, uint8_t decimate);

/**
 * @brief  Set ambient cancellation DAC code, from 0 to 300uA with 0.6uA/LSB, if current is 10, it means 0.6 * 10 = 6uA.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel 0 or 1
 * @param  current         Current code, current = current code * 0.6uA
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_ambientdac(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, uint16_t current);

/**
 * @brief  Set LED DC offset cancellation DAC code, from 0 to 190uA with 1.5uA/LSB. Set to 0 to disable.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  channel         Channel 0 or 1
 * @param  current         Current code, current = current code * 1.5uA
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_dcdac(adi_adpd6000_device_t *device, uint8_t slot, uint8_t channel, uint8_t current);

/**
 * @brief  Set timeslot data size.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  signal_size     Signal size
 * @param  lit_size        Lit size
 * @param  dark_size       Dark size
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_data_size(adi_adpd6000_device_t *device, uint8_t slot, uint8_t signal_size, uint8_t lit_size, uint8_t dark_size);

/**
 * @brief  Set timeslot data shift.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  signal_offset   Signal shift
 * @param  lit_offset      Lit shift
 * @param  dark_offset     Dark shift
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_window_offset(adi_adpd6000_device_t *device, uint8_t slot, uint8_t signal_offset, uint8_t lit_offset, uint8_t dark_offset);

/**
 * @brief  Set timeslot ambient callelation mode.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  alc_type        @see adi_adpd6000_ppg_alc_type_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_alctype(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_alc_type_e alc_type);

/**
 * @brief  Set timeslot ambient_callelation coarse loop width.
 *         
 * @param  device          Pointer to device structure
 * @param  slot            Time slot index
 * @param  alc_loop_width  @see adi_adpd6000_ppg_alc_loop_width_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_alc_loop_width(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_alc_loop_width_e alc_loop_width);

/**
 * @brief  Set timeslot sample mode.
 *         
 * @param  device      Pointer to device structure
 * @param  slot        Time slot index
 * @param  type        @see adi_adpd6000_ppg_sample_type_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_set_sample_type(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_ppg_sample_type_e type);

/**
 * @brief  Read PPG data from FIFO based on FIFO configuration
 *         
 * @param  device            Pointer to device structure
 * @param  fifo              @see adi_adpd6000_fifo_config_t
 * @param  signal_data       Pointer to PPG signal data, if null, do not output the data
 * @param  dark_data         Pointer to PPG dark data, if null, do not output the data
 * @param  lit_data          Pointer to PPG lit data, if null, do not output the data
 * @param  ppg_num           PPG channel number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_read_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *signal_data, uint32_t *dark_data, uint32_t *lit_data, uint8_t *ppg_num);

/**
 * @brief  Read PPG data from FIFO based on FIFO configuration
 *         
 * @param  device            Pointer to device structure
 * @param  fifo              @see adi_adpd6000_fifo_config_t
 * @param  signal_data       Pointer to PPG signal data, if null, do not output the data
 * @param  dark_data         Pointer to PPG dark data, if null, do not output the data
 * @param  lit_data          Pointer to PPG lit data, if null, do not output the data
 * @param  slot_num          PPG slot number
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_read_struct_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_slot_data_t *signal_data, adi_adpd6000_ppg_slot_data_t *dark_data, adi_adpd6000_ppg_slot_data_t *lit_data, uint8_t *slot_num);

/**
 * @brief  Init AGC configuration, fifo and ppg_cfg should be initilized before calling the function
 *         
 * @param  device            Pointer to device structure
 * @param  fifo              @see adi_adpd6000_fifo_config_t
 * @param  ppg_cfg           @see adi_adpd6000_ppg_agc_cfg_t
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_agc_init(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_agc_cfg_t *ppg_cfg);

/**
 * @brief  AGC process function, fifo and ppg_cfg should be initilized before calling the function
 *         
 * @param  device            Pointer to device structure
 * @param  fifo              @see adi_adpd6000_fifo_config_t
 * @param  ppg_cfg           @see adi_adpd6000_ppg_agc_cfg_t
 * @param  signal_data       Pointer to signal data
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_ppg_agc_process(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_ppg_agc_cfg_t *ppg_cfg, uint32_t *signal_data);

/**
 * @brief  Set BioZ time slot mode.
 *         
 * @param  device     Pointer to device structure
 * @param  mode       @see adi_adpd6000_bioz_slot_mode_e
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
// int32_t adi_adpd6000_bioz_set_slot_mode(adi_adpd6000_device_t *device, adi_adpd6000_bioz_slot_mode_e mode);

// /**
//  * @brief  Get BioZ aviable channels number
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot_num    Pointer to channel number
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_get_slot_num(adi_adpd6000_device_t *device, uint8_t *slot_num);

// /**
//  * @brief  Set BioZ timeslot offset
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  offset      Offset data
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_set_timeslot_offset(adi_adpd6000_device_t *device, uint8_t slot, uint16_t offset);

// /**
//  * @brief  Config sinusoid amplitude, drequency, phase and offset
//  *         To get accurate DFT result and avoid spectral leakage, recommend freq/(DFT_FS/N) to be integer. N means the number of DFT input data, DFT_FS means DFT input data rate. DFT_FS can be different due to different input data sources.
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  amp         Sinusoid Scales
//  * @param  freq        Sinusoid Frequency, unit in Hz
//  * @param  phase       Sinusoid offset, unit in degree
//  * @param  offset      Sinusoid Phase Offset, unit in degree
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_cfg_wave(adi_adpd6000_device_t *device, uint8_t slot, uint16_t amp, uint32_t freq, uint16_t phase, uint16_t offset);

// /**
//  * @brief  Config HPTIA feedback resistor and cap.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  res         resistor, @see adi_adpd6000_bioz_tia_gain_res_e
//  * @param  cap         cap value is from 0 to 15, 0 means off, unit in pf
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_tia_set_gain(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_tia_gain_res_e res, uint8_t cap);

// /**
//  * @brief  Connect/disconnect Rcal to excbuf.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable tia low power mode, true - enable tia low power mode.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_tia_enable_lowpower(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Set DAC gain.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  gain        DAC Gain Correction Factor.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_dac_set_gain(adi_adpd6000_device_t *device, uint8_t slot, uint16_t gain);

// /**
//  * @brief  Bypass SINC3 filter.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  en_bypass   true - Bypass SINC3 filter.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_bypass_sinc3_filter(adi_adpd6000_device_t *device, uint8_t slot, bool en_bypass);

// /**
//  * @brief  Set SINC3 filter oversample rate.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  rate        @see adi_adpd6000_bioz_sinc3_os_rate_e.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_sinc3_set_sample_rate(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_sinc3_os_rate_e rate);

// /**
//  * @brief  Enable/disable average function.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable average, true - enable average.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_enable_average(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Sets number of samples used by averaging function.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  sample      @see adi_adpd6000_bioz_sinc3_avg_e.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_set_average_sample(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_sinc3_avg_e sample);

// /**
//  * @brief  Set DFT phase offset.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  phase       unit in degree.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_dft_set_phase(adi_adpd6000_device_t *device, uint8_t slot, uint16_t phase);

// /**
//  * @brief  Set DFT pointer number.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  num         @see adi_adpd6000_bioz_dft_point_e.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_dft_set_point_number(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_dft_point_e num);

// /**
//  * @brief  Enable/disable hanning window.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable hanning window, true - enable hanning winodw.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_dft_enable_hanning(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Enable/disable DAC reference.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable DAC reference, true - enable DAC reference.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_enable_dac_ref(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Enable/disable High Power TIA.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable High Power TIA, true - enable High Power TIA.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_enable_tia(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Enable/disable Excitation Buffer to drive the resistance under measurement..
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - disable Excitation Buffer, true - enable Excitation Buffer.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_enable_exbuf(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Power up/down PGA.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  enable      false - Power down PGA, true - Power up PGA.
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_enable_pga(adi_adpd6000_device_t *device, uint8_t slot, bool enable);

// /**
//  * @brief  Set front buffer mode.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  mode        @see adi_adpd6000_bioz_amp_mode_e
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_set_amp_mode(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_amp_mode_e mode);

// /**
//  * @brief  Set timeslot connection.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  mode        @see adi_adpd6000_bioz_slot_connect_e
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_set_slot_connection(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_slot_connect_e mode);

// /**
//  * @brief  Set bio-impedance exbuf input.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  input       @see adi_adpd6000_bioz_imp_pin_e
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_sel_exbuf_input(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_imp_pin_e input);

// /**
//  * @brief  Set bio-impedance hptia input.
//  *         
//  * @param  device      Pointer to device structure
//  * @param  slot        Time slot index
//  * @param  input       @see adi_adpd6000_bioz_imp_pin_e
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_sel_tia_input(adi_adpd6000_device_t *device, uint8_t slot, adi_adpd6000_bioz_imp_pin_e input);

// /**
//  * @brief  Read BioZ data from FIFO based on FIFO configuration
//  *         
//  * @param  device            Pointer to device structure
//  * @param  fifo              @see adi_adpd6000_fifo_config_t
//  * @param  bioz_real         Pointer to BioZ real data
//  * @param  bioz_imag         Pointer to BioZ image data
//  * @param  bioz_num          BioZ channel number
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_read_fifo(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, uint32_t *bioz_real, uint32_t *bioz_imag, uint8_t *bioz_num);

// /**
//  * @brief  Read BioZ data from FIFO based on FIFO configuration
//  *         
//  * @param  device            Pointer to device structure
//  * @param  fifo              @see adi_adpd6000_fifo_config_t
//  * @param  bioz              @see adi_adpd6000_bioz_slot_data_t
//  * @param  bioz_num          BioZ channel number
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_read_fifo_struct(adi_adpd6000_device_t *device, adi_adpd6000_fifo_config_t *fifo, adi_adpd6000_bioz_slot_data_t *bioz, uint8_t *bioz_num);

// /**
//  * @brief  Cal amp and phase based on 6 timeslts mode
//  *         
//  * @param  Real              Real data pointer
//  * @param  Image             Image data pointer
//  * @param  amp               Pointer to amp
//  * @param  phase             Pointer to phase
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_cal_ms6(uint32_t Real[6], uint32_t Image[6], float *amp, float *phase);

// /**
//  * @brief  Cal amp and phase based on 4 timeslts mode
//  *         
//  * @param  Real              Real data pointer
//  * @param  Image             Image data pointer
//  * @param  amp               Pointer to amp
//  * @param  phase             Pointer to phase
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_cal_ms4(uint32_t Real[4], uint32_t image[4], float *amp, float *phase);

// /**
//  * @brief  Set cal resistor value, default value of the resistor is 2K
//  *         
//  * @param  rcal                 Resistor value
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_set_rcal(float rcal);

// /**
//  * @brief  Get current cal resistor value
//  *         
//  * @param  rcal                 Pointer to resistor value
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_get_rcal(float *rcal);

// /**
//  * @brief  Get internal cal resistor value, cal the function when sequence is stopped
//  *         
//  * @param  rcal                 Pointer to resistor value
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
// int32_t adi_adpd6000_bioz_get_internal_rcal(adi_adpd6000_device_t *device, float *rcal);

// /**
//  * @brief  Set GPIO mode
//  *         
//  * @param  device            Pointer to device structure
//  * @param  index             Gpio index from 0 ~ 3
//  * @param  mode              @see adi_adpd6000_gpio_mode_e
//  *
//  * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
//  */
int32_t adi_adpd6000_gpio_set_mode(adi_adpd6000_device_t *device, uint8_t index, adi_adpd6000_gpio_mode_e mode);

/**
 * @brief  Set GPIO output mode
 *         
 * @param  device            Pointer to device structure
 * @param  index             Gpio index from 0 ~ 3
 * @param  output            GPIO output select, please refer to datasheet
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_gpio_set_output(adi_adpd6000_device_t *device, uint8_t index, uint8_t output);

/**
 * @brief  Get GPIO input
 *         
 * @param  device            Pointer to device structure
 * @param  index             Gpio index from 0 ~ 3
 * @param  input             Pointer to input
 *
 * @return API_ADPD6000_ERROR_OK for success, @see adi_adpd6000_error_e
 */
int32_t adi_adpd6000_gpio_get_input(adi_adpd6000_device_t *device, uint8_t index, uint8_t *input);

#ifdef __cplusplus
}
#endif

#endif  /*__ADI_ADPD6000_H__*/
/*! @} */
