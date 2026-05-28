#include <Arduino.h>
#include "PAM.h"
#include "src/devices_init.h"
#include "src/adpd/u_adpd6100.h"
#include "src/as7341/spec_meas.h"
#include "src/mlx90632/u_mlx.h"


extern ADPD6 adpd;
static char STR_SUC[] = "Success";
static char STR_FAIL[] = "Failed";


char* str_results[3] = {STR_FAIL, STR_SUC, STR_FAIL};


static int check_adpd(){
    uint8_t ret = (uint8_t)adpd.begin();
    if (ret < 2) Serial.printf("ADPD check %s", str_results[ret]);
    return ret;
}

static int check_spec(){
    uint8_t ret = (uint8_t)check_AS7341();
    if (ret < 2) Serial.printf("AS7341 check %s", str_results[ret]);
    return ret;
}


static int check_mlx(){
    uint8_t ret = (uint8_t)mlx_init();
    if (ret < 2) Serial.printf("MLX90632 check %s", str_results[ret]);
    return ret;
}


static int test_optic_path(){
    adpd.STOP();
    conf_slow_FR_1(40, 20, 0, 5, 5, 5, 5, 5, 3);

    adpd.num_ts(3);
    uint8_t expected_readout_bytes = 24;
    uint8_t expected_readout = 8;
    uint32_t ret[expected_readout] = {0};
    uint16_t fifo_c = 0;
    uint32_t counter = 0;
    uint32_t num_ptx = 500;

    AS_LED_OFF();
    AS_LED_Current(50);
    adpd.run_freq(50);

    adpd.RUN();
    while (counter < num_ptx){
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){ // read all bytes from FIFO
            adpd.readfifo(expected_readout, 3, ret);
            fifo_c -= expected_readout_bytes;
            if (counter == num_ptx) break;
            Serial.printf("%d,%d,%d,%d,%d,%d,%d,%d\n", ret[0],ret[1],ret[2],ret[3],ret[4],ret[5],ret[6],ret[7]);
            counter++;
            if (counter == 250) AS_LED_ON();
            if (counter == 480) AS_LED_OFF();
        }        
    }

    adpd.STOP();
    AS_LED_OFF();
    AS_LED_Current(0);
    return 0;
}




int check_connections(){
    // check_adpd();
    // check_spec();
    // check_mlx();
    double temperature_before, temperature_after, t;


    mlx_measure(&t, &temperature_before);
    test_optic_path();
    mlx_measure(&t, &temperature_after);


    Serial.printf("#obj:%f-%f-%f\n", t, temperature_before, temperature_after);


    return 0;
}


int optic_test(){
    adpd.STOP();
    conf_slow_FR_1(100, 20, 0, 1, 5, 5, 5, 5, 1);

    adpd.num_ts(3);
    uint8_t expected_readout_bytes = 24;
    uint8_t expected_readout = 8;
    uint32_t ret[expected_readout] = {0};
    uint16_t fifo_c = 0;
    uint32_t counter = 0;
    uint32_t num_ptx = 2000;
    uint32_t sig = 0;

    AS_LED_OFF();
    AS_LED_Current(50);
    adpd.run_freq(25);

    adpd.RUN();
    while (counter < num_ptx){
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){ // read all bytes from FIFO
            adpd.readfifo(expected_readout, 3, ret);
            fifo_c -= expected_readout_bytes;
            if (counter == num_ptx) break;
            sig = calc_signal(ret[2], ret[3], 1);
            Serial.printf("%d,%d,%d,%d,%d,%d,%d\n", ret[0]-65000,ret[1]-65000,ret[2]-16000,ret[3]-16000,ret[4]-16000,ret[5]-16000,sig);
            counter++;
        }        
    }

    adpd.STOP();
    AS_LED_OFF();
    AS_LED_Current(0);
    return 0;
}


int optic_test(uint8_t current, uint8_t num_integ, uint8_t lit_offset, uint8_t dark1_offset, uint8_t dark2_offset, uint8_t pulse_offset, uint8_t pulse_duration){
    adpd.STOP();
    fluor_offset_test(current, num_integ, lit_offset, dark1_offset, dark2_offset, pulse_offset, pulse_duration);

    adpd.num_ts(1);
    uint8_t expected_readout_bytes = 12;
    uint8_t expected_readout = 4;
    uint32_t ret[expected_readout] = {0};
    uint16_t fifo_c = 0;
    uint32_t counter = 0;
    uint32_t num_ptx = 300;
    uint32_t sig = 0;

    AS_LED_OFF();
    AS_LED_Current(100);
    adpd.run_freq(25);

    adpd.RUN();
    while (counter < num_ptx){
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){ // read all bytes from FIFO
            adpd.readfifo(expected_readout, 3, ret);
            fifo_c -= expected_readout_bytes;
            if (counter == num_ptx) break;
            if (counter == 100) AS_LED_ON();
            if (counter == 200) AS_LED_OFF();
            sig = calc_signal(ret[0], ret[1], num_integ);
            Serial.printf("%d,%d,%d,%d,%d\n", ret[0]-16000*num_integ, ret[1]-16000*num_integ,ret[2]-16000*num_integ,ret[3]-16000*num_integ, ret[1] - ret[0]);
            counter++;
        }        
    }

    adpd.STOP();
    AS_LED_OFF();
    AS_LED_Current(0);
    return 0;
}

