#ifndef _PAM_H_
#define _PAM_H_
#include "src/adpd/u_adpd6100.h"
#include "src/as7341/spec_meas.h"
#include "src/devices_init.h"
#include "src/mlx90632/u_mlx.h"
#include "data_utils.h"
#include <Arduino.h>


extern ADPD6 adpd;
extern uint8_t adpd_mode;

typedef struct{
    bool init = false;
    uint8_t I620 = 110;
    uint8_t I720 = 40;
    uint8_t IR = 20;
} adpd_current_config_t;

typedef struct {
    bool init = false;
    uint8_t Fluo = 1;
    uint8_t FluoRef = 5;
    uint8_t IR = 5;
    uint8_t IRRef = 1;
    uint8_t Sun = 5;
    uint8_t Leaf = 5;
} adpd_gains_config_t;


extern adpd_current_config_t adpd_current_config;
extern adpd_gains_config_t adpd_gains_config;



int conf_slow_FR_1(uint8_t I620, uint8_t I730, uint8_t I_FR, uint8_t G_Fluor, uint8_t G_FluorRef, uint8_t G_Sun, uint8_t G_IR, uint8_t G_FR, uint8_t G_FRref);
int conf_slow_FR_1(void);
int fluor_offset_test(uint8_t current, uint8_t num_integ, uint8_t lit_offset, uint8_t dark1_offset, uint8_t dark2_offset, uint8_t pulse_offset, uint8_t pulse_duration);

int MPF(uint16_t mode, uint16_t current, uint16_t dc_current, uint8_t sign_gain, uint8_t ref_gain);
int MPF(uint16_t mode, uint16_t dc_current);
int run_arr_type1(uint8_t length, uint8_t* arr, bool led_persist);
int run_arr_type1(uint8_t length, uint8_t* arr, bool led_persist, bool allow_interrupt);
int run_trigger_spacer(uint16_t length, uint8_t interval, bool change_act, uint8_t act, bool interrrupt);
uint32_t PAM_get_env(uint8_t mode, unsigned int t0);
uint32_t PAM_retrieve_env(uint32_t r, uint8_t* mode, float_t* data_f = NULL, int16_t* data_i = NULL);
int fluor_offset(uint32_t* ret);
enum ADPD_CONFIG_MODE {
    MPF_MODE,
    IDLE,
    ARRAY_MODE1,
    ARRAY_SLOW,
    FUTURE
};

#endif