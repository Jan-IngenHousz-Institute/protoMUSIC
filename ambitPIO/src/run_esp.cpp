#include <Arduino.h>
#include "data_utils.h"
#include "src/as7341/spec_meas.h"
#include "src/mlx90632/u_mlx.h"
#include "PAM.h"
#include <Preferences.h>
#include "nvs1.h"

#define TAG "ESP"
#define ESP_CMD_HEADER 160
#define ESP_CMD_DONE 161
#define ESP_CMD_END 240
#define ESP_WAKE_FOR_CMD 170
#define MAX_ARR_LEN 16





// keep a local copy of settings for run-time change
static adpd_current_config_t adpd_current_config_local;
static adpd_gains_config_t adpd_gains_config_local;
extern Preferences preferences;

extern uint8_t CONNECTION_TYPE;
int serial_read_until(uint8_t target1, uint8_t target2 = 0, uint8_t target3 = 0, uint16_t timeout = 20, bool remove = false);

// extern char ambit_name[];
// float_t actinic_coef = 1.0;
// float_t spec_coef = 1.0;
// extern double mlx_emissivity;


int do_esp_cmd(){
    uint8_t cmd_arr[8], c, ret;
    ESP_LOGV(TAG, "parse ESP commands");

    //search for cmd header
    ret = serial_read_until(ESP_CMD_HEADER, 0, 0, 100, true);
    if (ret != 1){
        ESP_LOGE(TAG, "ESP cmd header failed");
        return -1;
    }

    CONNECTION_TYPE = CONNECTION_TYPES::AMBYTE;

    // the real cmd will follows
    c = Serial.readBytes(cmd_arr, 8);
    if (c < 8){
        ESP_LOGE(TAG, "ESP cmd parse failed");
        return -1;        
    }
    //Serial.printf("cmd is %d, %d, %d, %d, %d, %d, %d, %d\n", cmd_arr[0], cmd_arr[1], cmd_arr[2], cmd_arr[3], cmd_arr[4], cmd_arr[5], cmd_arr[6], cmd_arr[7]);

    switch (cmd_arr[0])
    {
    case 1:  // set PD gains
        if ((cmd_arr[1] > 0) && (cmd_arr[1] < 7)) adpd_gains_config_local.Fluo = cmd_arr[1] - 1;
        if ((cmd_arr[2] > 0) && (cmd_arr[2] < 7)) adpd_gains_config_local.FluoRef = cmd_arr[2] - 1;
        if ((cmd_arr[3] > 0) && (cmd_arr[3] < 7)) adpd_gains_config_local.IR = cmd_arr[3] - 1;
        if ((cmd_arr[4] > 0) && (cmd_arr[4] < 7)) adpd_gains_config_local.IRRef = cmd_arr[4] - 1;
        if ((cmd_arr[5] > 0) && (cmd_arr[5] < 7)) adpd_gains_config_local.Sun = cmd_arr[5] - 1;
        if ((cmd_arr[6] > 0) && (cmd_arr[6] < 7)) adpd_gains_config_local.Leaf = cmd_arr[6] - 1;
        adpd_gains_config_local.init = true;
        ESP_LOGV(TAG, "gains are %d, %d, %d, %d, %d, %d", adpd_gains_config_local.Fluo, adpd_gains_config_local.FluoRef, adpd_gains_config_local.IR, adpd_gains_config_local.IRRef, adpd_gains_config_local.Sun, adpd_gains_config_local.Leaf);
        Serial.write(ESP_CMD_DONE);
        break;

    case 2:  // set currents
        if ((cmd_arr[1] < 127)) adpd_current_config_local.I620 = cmd_arr[1];
        if ((cmd_arr[2] < 127)) adpd_current_config_local.I720 = cmd_arr[2];
        if ((cmd_arr[3] < 127)) adpd_current_config_local.IR = cmd_arr[3];
        adpd_current_config_local.init = true;
        ESP_LOGV(TAG, "currents are %d, %d, %d", adpd_current_config_local.I620, adpd_current_config_local.I720, adpd_current_config_local.IR);
        Serial.write(ESP_CMD_DONE);
        break;    
    
    case 10: // array run config
        if (adpd_gains_config_local.init == false) ESP_LOGE(TAG, "Gain preset not initized, use default!");
        if (adpd_current_config_local.init == false) ESP_LOGE(TAG, "Current preset not initized, use default!");
        conf_slow_FR_1(adpd_current_config_local.I620, adpd_current_config_local.I720, adpd_current_config_local.IR, 
            adpd_gains_config_local.Fluo, adpd_gains_config_local.FluoRef, adpd_gains_config_local.Sun, adpd_gains_config_local.Leaf, adpd_gains_config_local.IR, adpd_gains_config_local.IRRef);
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
        Serial.write(ESP_CMD_DONE);
        break;

    case 20: // run mpf
    {
        if (adpd_gains_config_local.init == false) ESP_LOGE(TAG, "Gain preset not initized, use default!");
        if (adpd_current_config_local.init == false) ESP_LOGE(TAG, "Current preset not initized, use default!");

        uint16_t length = (((uint16_t) cmd_arr[1]) << 7) + cmd_arr[2];
        uint8_t interval = cmd_arr[3];
        bool change_act = (bool) cmd_arr[4];
        uint8_t act = cmd_arr[5];

        //Serial.printf("%d, %d, %d, %d\n", length, interval, change_act, act);
        
        Serial.write(ESP_CMD_DONE);
        run_trigger_spacer(length, interval, change_act, act, true);
        Serial.write(ESP_CMD_END);
    }    
        break;

    case 21:// run
    {   
        uint8_t arr_length = cmd_arr[1];
        uint8_t led_persist = cmd_arr[2];
        bool allow_interrupt = (bool) cmd_arr[3];

        uint8_t cc = 0;
        if ((arr_length == 0) || (arr_length > MAX_ARR_LEN)){
            ESP_LOGE(TAG, "run array wrong length: %d", arr_length);
            break;
        }
        uint8_t run_arr[arr_length * 8];
        if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
            if (adpd_gains_config_local.init == false) ESP_LOGE(TAG, "Gain preset not initized, use default!");
            if (adpd_current_config_local.init == false) ESP_LOGE(TAG, "Current preset not initized, use default!");
            conf_slow_FR_1(adpd_current_config_local.I620, adpd_current_config_local.I720, adpd_current_config_local.IR, 
                adpd_gains_config_local.Fluo, adpd_gains_config_local.FluoRef, adpd_gains_config_local.Sun, adpd_gains_config_local.Leaf, adpd_gains_config_local.IR, adpd_gains_config_local.IRRef);
            adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
        }

        cc = Serial.readBytes(run_arr, arr_length * 8);
        if (cc != (arr_length * 8)){
            ESP_LOGE(TAG, "run array elements count %d not match config %d", cc, arr_length);
            break;
        }
        Serial.write(ESP_CMD_DONE);
        run_arr_type1(arr_length, run_arr, led_persist, allow_interrupt);
        Serial.write(ESP_CMD_END);
        
    }
    break;

    case 31: // get spec
    {
        uint16_t spec[12] = {0};
        float par = get_PAR(spec);
        memcpy(spec + 10, &par, 4);
        Serial.write(ESP_CMD_DONE);
        Serial.write((uint8_t*) spec, 24);
        Serial.write(ESP_CMD_END);
    }
    break;

    case 32: // get temp
    {
        double leaf, chip;
        mlx_measure(&leaf, &chip);
        Serial.write(ESP_CMD_DONE);
        int16_t t1 = (int16_t) (leaf * 10);
        int16_t t2 = (int16_t) (chip * 10);
        Serial.write((uint8_t*) (&t1), 2);
        Serial.write((uint8_t*) (&t2), 2);
        Serial.write(ESP_CMD_END);
    }
    break;

    case 33:    // retrieve ambit info
    {
        if (cmd_arr[1] == 1){ // send calibration info
            Serial.write(ESP_CMD_DONE);
            Serial.write((uint8_t*) &ambit_calibration_local, sizeof(ambit_calibration_info_t));
            Serial.write(ESP_CMD_END);
        }else if (cmd_arr[1] == 2){ // send FW info
            Serial.write(ESP_CMD_DONE);
            Serial.write((uint8_t*) &ambit_FW_info, sizeof(ambit_FW_info_t));
            Serial.write(ESP_CMD_END);
        }else if (cmd_arr[1] == 3){ // send metadata
            Serial.write(ESP_CMD_DONE);
            Serial.write((uint8_t*) &metadata_epprom, sizeof(metadata_t));
            Serial.write(ESP_CMD_END);
        }else{
            Serial.write(ESP_CMD_END);
        }
    }
    break;

    

    
    case 34: // get temp and raw
    {
        double leaf, leaf_1, chip;
        int16_t a1, a2, a3, a4;
        mlx_measure(&leaf, &chip, &leaf_1, &a1, &a2, &a3, &a4);
        Serial.write(ESP_CMD_DONE);
        int16_t t1 = (int16_t) (leaf * 10);
        int16_t t2 = (int16_t) (leaf_1 * 10);
        int16_t t3 = (int16_t) (chip * 10);
        Serial.write((uint8_t*) (&t1), 2);
        Serial.write((uint8_t*) (&t2), 2);
        Serial.write((uint8_t*) (&t3), 2);
        Serial.write((uint8_t*) (&a1), 2);
        Serial.write((uint8_t*) (&a2), 2);
        Serial.write((uint8_t*) (&a3), 2);
        Serial.write((uint8_t*) (&a4), 2);
        Serial.write(ESP_CMD_END);
    }
    break;

    
    case 37:    // set metadata
    {        
        Serial.write(ESP_CMD_DONE);
        Serial.readBytes((uint8_t*) (&metadata_incoming), sizeof(metadata_t));
        Serial.write(ESP_CMD_END);
        if (metadata_incoming.EOF_MARK == 2025) save_metadata();
        load_info_from_nvs(false);
    }
    break;



    case 4: // try/set actinic
    {
        AS_LED_OFF();
        Serial.write(ESP_CMD_DONE);
        uint8_t type = cmd_arr[1];
        uint8_t var = cmd_arr[2];
        uint8_t var2 = cmd_arr[3];
        float_t _factor = 1.0;
        if (type == 1){ // try actinics
            AS_LED_Current(50);
            AS_LED_ON();
            delay(3000);
            AS_LED_Current(var);
            delay(3000);
            AS_LED_OFF();            
            AS_LED_Current(0);
        }else if (type == 2){ // set actinic offset
            _factor = *((float *) &(cmd_arr[3]));
            if ((_factor > 0.01) && (_factor < 1.01)){
                preferences.begin("config", false);
                preferences.putFloat("actinic", _factor);
                preferences.end();
                ambit_calibration_local.actinic_coef = _factor;
            }
        }else if (type == 4){ // set actinic offset
            _factor = *((float *) &(cmd_arr[3]));
            if ((_factor > 0.05) && (_factor < 100.01)){
                //preferences.begin("config", false);
                //preferences.putFloat("spec", _factor);
                //preferences.end();
                ambit_calibration_local.spec_coef = _factor;
            }
        }else if (type == 5){
            AS_LED_Current(var);
            AS_LED_ON();
            delay(var2 * 100);
            AS_LED_OFF();
        }
        Serial.write(ESP_CMD_END);
    }
    break;

    
    case 5:
    {
        uint8_t ambit_id = cmd_arr[1];
        uint8_t intensity = cmd_arr[2];
        Serial.write(ESP_CMD_DONE);
        if ((ambit_id < 4) && (intensity > 4) && (intensity < 254)) as7431_blink(ambit_id, intensity);
        Serial.write(ESP_CMD_END);
    }
    break;

    case 6: // do adpd baseline flash
    {
        Serial.write(ESP_CMD_DONE);
        uint32_t ret[6] = {0};
        fluor_offset(ret);
        preferences.begin("config", false);
        preferences.putUInt("adpd_lit", ret[1]);
        preferences.putUInt("adpd_sun", ret[2]);
        preferences.putUInt("adpd_leaf", ret[3]);
        preferences.putUInt("adpd_730", ret[4]);
        preferences.putUInt("adpd_730r", ret[5]);
        preferences.end();
        Serial.write(ESP_CMD_END);
        load_info_from_nvs(false);

    }
    break;

    case 17: // nvs update scalar
    {
        uint8_t type = cmd_arr[1]; // 1: actinic 
        uint8_t dtype = cmd_arr[2]; // 1: float
        if ((type == 1) && (dtype == 1)){ // update actinic coef
            Serial.write(ESP_CMD_DONE);
            float_t _factor = *((float *) &(cmd_arr[3]));
            if ((_factor > 0.01) && (_factor < 1.01)){
                preferences.begin("config", false);
                preferences.putFloat("actinic", _factor);
                preferences.end();
                ambit_calibration_local.actinic_coef = _factor;
                load_info_from_nvs(false);
            }
            Serial.write(ESP_CMD_END);
        }

    }   
    break; 

    case 18: // nvs update array
    {
        uint8_t type = cmd_arr[1]; // 1: actinic linear test
        float_t _factor = 1.0;
        if (type == 1){ // update actinic linear readings
            uint16_t _readingsf[6] = {0};
            Serial.write(ESP_CMD_DONE);
            Serial.readBytes((uint8_t*) _readingsf, 12);
            uint16_t checksum = 0;
            _factor = *((float *) &(cmd_arr[4]));
            for (int i = 0; i < 5; i++){
                checksum += _readingsf[i];
            }
            if (checksum == _readingsf[5]){
                preferences.begin("config", false);
                preferences.putUShort("act_50", _readingsf[0]);
                preferences.putUShort("act_100", _readingsf[1]);
                preferences.putUShort("act_150", _readingsf[2]);
                preferences.putUShort("act_200", _readingsf[3]);
                preferences.putUShort("act_250", _readingsf[4]);
                preferences.putFloat("actinic", _factor);
                preferences.end();
                load_info_from_nvs(false);                
            }
            Serial.write(ESP_CMD_END);
        }

    }   
    break; 






    default:
        ESP_LOGE(TAG, "Bad command");
        break;
    }

    return 0;
}

