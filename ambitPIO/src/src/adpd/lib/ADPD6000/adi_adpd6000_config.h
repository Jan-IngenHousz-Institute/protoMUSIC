/*!
 * @brief     ADPD6000/ADPD6000-LITE Configuration Header
 * @copyright Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 */
 
/*!
 * @addtogroup adi_adpd6000_sdk
 * @{
 */

#ifndef __ADI_ADPD6000_CONFIG_H__
#define __ADI_ADPD6000_CONFIG_H__

/*============= D E F I N E S ==============*/
/*!< verbose log report control, bit0~15: level, bit16~31: content */
#define ADPD6000_LOG_ERR_MSG       0x00000001       /*!< error message */
#define ADPD6000_LOG_WARN_MSG      0x00000002       /*!< warning message */
#define ADPD6000_LOG_INFO_MSG      0x00000004       /*!< info message */
#define ADPD6000_LOG_MISC_MSG      0x00010000       /*!< miscellaneous message */ 
#define ADPD6000_LOG_FUNC_MSG      0x00020000       /*!< function call message */
#define ADPD6000_LOG_VAR_MSG       0x00040000       /*!< variable related message */
#define ADPD6000_LOG_REG_MSG       0x00080000       /*!< register r/w  message */

#define ADPD6000_LOG_NONE_MSG      0x00000000       /*!< all not selected */
#define ADPD6000_LOG_ALL_MSG       0xffffffff       /*!< all selected */

#ifndef ADPD6000_REPORT_VERBOSE
#define ADPD6000_REPORT_VERBOSE    0                /*!< actual log control */
#endif

/*!< max buffer size that sdk is using internally for control port access */
#define ADPD6000_SDK_MAX_BUFSIZE   16               /*!< buffer size sdk allocates for control port access */

#endif /* __ADI_ADPD6000_CONFIG_H__ */

/*! @} */
