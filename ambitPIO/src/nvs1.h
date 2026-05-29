#ifndef _NVS1_H_
#define _NVS1_H_
#include <Arduino.h>
#include "nvs_flash.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 0
#define BATCH_VERSION 3

struct ambit_calibration_info_t
{
    char ambit_name[20] = "AmbitV003";
    int32_t mlx_coef[14] = {0};
    uint32_t adpd[6] = {0};
    float_t temp_offset = 0.0;
    float_t temp_slope = 1.0;
    float_t actinic_coef = 0.1;
    float_t spec_coef = 1.0;
    uint16_t act_50 = 5;
    uint16_t act_100 = 4;
    uint16_t act_150 = 3;
    uint16_t act_200 = 2;
    uint16_t act_250 = 1;
    float_t mlx_emissivity = 1.0;
    float_t sun_coef = 1.0;
    float_t tick_factor = 0.854;  // PAM point-period scale (ms tick -> s); surfaced to the ambyte
};

extern struct ambit_calibration_info_t ambit_calibration_local, ambit_calibration_income;

struct ambit_FW_info_t
{
    uint8_t Major = MAJOR_VERSION;
    uint8_t Minor = MINOR_VERSION;
    uint8_t Batch = BATCH_VERSION;
    uint32_t Size = 0;
    uint64_t MAC = 0;
    char FW_date[12];
    char reserved[12];
    uint8_t Checksum = 0;
};

extern struct ambit_FW_info_t ambit_FW_info;

struct metadata_t
{
    double lon = 1.0;
    double lat = 1.0;
    float alt = 1.0;
    float acc = 1.0;
    float vacc = 1.0;
    uint32_t time = 0;
    float x = 0.0;
    float y = 0.0;
    float z = 0.0;
    char info1[200] = "New_Ambit";
    uint16_t EOF_MARK = 2025; // end of file marker, used to check if the metadata is valid
};

extern struct metadata_t metadata_epprom, metadata_incoming;

void load_info_from_nvs(bool print);
void save_metadata(void);

#endif // _NVS1_H_