
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "serial.h"
#include "src/mlx90632/u_mlx.h"
#include "src/as7341/spec_meas.h"
#include "src/wrench.h"
#include "data_utils.h"
#include "PAM.h"
#include "nvs1.h"
#include <Preferences.h>

int check_connections();
extern Preferences preferences;
extern bool FLAG_DEICE;
static const char* TAG1 = "do_Cmd";
void do_c(const char* c);

constexpr unsigned hash(const char *string)
{
 return *string == 0 ? 17325 : *string + (*string * hash(string+1));
}

void do_command(char *choose){

    int setting;
  // char choose[50]; //buffer to hold commands

  //Serial_Input_Chars(choose, "+", 500, sizeof(choose) - 1);

  for (unsigned i = 0; i < strlen(choose); ++i) {   // remove ctrl characters
    if (!isprint(choose[i]))
      choose[i] = 0;
  }

  if (strlen(choose) < 1) {         // short or null command, quietly ignore it
    return;
  }

  if (!isalnum(choose[0])) {
    // Serial_Printf("error: not a-n\n");
    return;                         // go read another command
  }

  unsigned val;                     // we accept int or alpha commands
  if (isdigit(choose[0])) {
    val = atoi(choose);
  }
  else {
    val = hash(choose);             // convert alpha command to an int
  }


  //Serial.printf("cmd: %s\n", choose);
  // process single commands
  switch (val) {
    case hash("C"):{

      char c[500];
      Serial_Input_Chars(c, "?", 10000, 500);
      do_c(c);
      //Serial.println();
    }
    break;


    case hash("hello"):
     {
      Serial.print("NEW Name Here");
      Serial.println(" Ready");
    }                                                                   
      break;

    int external_trigger_run(void);
    case hash("ww"):
     {
      Serial.println("Start");
      external_trigger_run();
      Serial.println("Exit");

    }                                                                   
      break;

    int external_trigger_run_Flash(unsigned int gate_time, unsigned int dt, const uint16_t num);
    case hash("ff"):
     {
      unsigned int t = Serial_Input_Long(",", 10);
      unsigned int dt = Serial_Input_Long(",", 10);
      unsigned int num = Serial_Input_Long(",", 10);
      external_trigger_run_Flash(t, dt, num);

    }                                                                   
      break;



    case hash("set_currents"):
     {
      adpd_current_config.I620 = (uint8_t) Serial_Input_Long(",", 10);
      adpd_current_config.I720 = (uint8_t) Serial_Input_Long(",", 10);
      adpd_current_config.IR = (uint8_t) Serial_Input_Long(",", 10);
      adpd_current_config.init = true;
      Serial.printf("Currents set to %d, %d, %d\n", adpd_current_config.I620, adpd_current_config.I720, adpd_current_config.IR);
      adpd_mode = ADPD_CONFIG_MODE::MPF_MODE; // not applied
     }                                                
    break;  

    case hash("set_gains"):
     {
      adpd_gains_config.Fluo = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.FluoRef = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.IR = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.IRRef = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.Sun = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.Leaf = (uint8_t) Serial_Input_Long(",", 10);
      adpd_gains_config.init = true;
      Serial.printf("Gains set to %d, %d, %d, %d, %d, %d\n", adpd_gains_config.Fluo, adpd_gains_config.FluoRef, adpd_gains_config.IR, adpd_gains_config.IRRef, adpd_gains_config.Sun, adpd_gains_config.Leaf);
      adpd_mode = ADPD_CONFIG_MODE::MPF_MODE; // not applied
     }                                                
    break;  

    case hash("mlx"):
    { 
      unsigned int timer = millis();
      for (uint8_t i = 0; i < 100; i++){
        Serial.println(mlx_measure());
      }
      Serial.printf("Spend %f ms per measurement", (millis() - timer)/100.0);
    }    
    break;

    case hash("temp"):
    { 
      // uint32_t ret;
      // uint8_t mode = 5;
      // float_t temp = 0.0;


      // ret = PAM_get_env(0, 500);
      // Serial.print(PAM_retrieve_env(ret, &mode));
      // Serial.printf(" %d \n", mode);

      // ret = PAM_get_env(4, 600);
      // Serial.println(PAM_retrieve_env(ret, &mode, &temp));
      // Serial.printf(" %d %f \n", mode, temp);
      double obj,amb,obj_r;
      int16_t a1,a2,a3,a4;

      mlx_measure(&obj, &amb, &obj_r, &a1, &a2, &a3, &a4);
      Serial.printf("%.4f\t%.4f\t%.4f\n", obj, amb, obj_r);


    }    
    break;

    case hash("clean_nvs"):
    {
      nvs_flash_erase();
      nvs_flash_init();
      Serial.println("NVS cleaned");
    }
    break;

    case hash("baseline"):
    {
      conf_slow_FR_1(100, 20, 0, 1, 5, 5, 1, 5, 5);
      uint8_t c = Serial_Input_Long(",", 10);
      adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      uint32_t ret[6] = {0};
      fluor_offset(ret);
      Serial.printf("%d,%d,%d,%d,%d,%d\n", ret[0], ret[1], ret[2], ret[3], ret[4], ret[5]);
      if (c == 1){
        if (ret[0] > 400){
          Serial.println("Baseline too high");
          break;
        }
        preferences.begin("config", false);
        preferences.putUInt("adpd_dark", ret[0]);
        preferences.end();
        Serial.println("Baseline saved");
      }
    }
    break;

    case hash("tttt"):
      Serial.println(sizeof(ambit_calibration_info_t));
    break;



    case hash("arrun"):
     {
      uint8_t len = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t persist = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t arr[128] = {0};
      uint8_t tmp_8 = 0;
      for (uint8_t i = 0; i < len; i++){
        for (uint8_t j = 0; j < 8; j++){
          arr[i * 8 + j] = (uint8_t) Serial_Input_Long(",", 10);
        }
      }

      if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
        conf_slow_FR_1();
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      }
      run_arr_type1(16, arr, persist);
    }
      break;  

    case hash("arrun1"):
     {
      uint8_t len = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t persist = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t arr[128] = {0};
      uint8_t tmp_8 = 0;
      CONNECTION_TYPE = CONNECTION_TYPES::PLOTTING;


      for (uint8_t i = 0; i < len; i++){
        for (uint8_t j = 0; j < 8; j++){
          arr[i * 8 + j] = (uint8_t) Serial_Input_Long(",", 10);
        }
      }

      if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
        conf_slow_FR_1();
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      }
      run_arr_type1(16, arr, persist);
    }
      Serial.println("Done");
      break;

    // arrun2 — like arrun1 but COMPUTER mode. Runs the whole trace, then dumps
    // each data array as one ASCII block ("Data:<tag>,Length:N\t v,v,...,")
    // ending with "Data sent". No per-point streaming → no inter-point UART
    // gaps, so it stays in sync on the ambyte (device-to-device) link.
    case hash("arrun2"):
     {
      uint8_t len = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t persist = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t arr[128] = {0};
      CONNECTION_TYPE = CONNECTION_TYPES::COMPUTER;

      for (uint8_t i = 0; i < len; i++){
        for (uint8_t j = 0; j < 8; j++){
          arr[i * 8 + j] = (uint8_t) Serial_Input_Long(",", 10);
        }
      }

      if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
        conf_slow_FR_1();
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      }
      run_arr_type1(16, arr, persist);
    }
      break;


      case hash("q"):
     {
      uint8_t a = (uint8_t) Serial_Input_Long(",", 10);
      
      uint8_t b = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t c = (uint8_t) Serial_Input_Long(",", 10);
      
      uint8_t arr[24] = {a, 0, 1, 0, 0, b, 0, 1, \
                        a, 0, 1, 0, 0, b, c, 1,\
                        a, 0, 1, 0, 0, b, 0, 1};

      CONNECTION_TYPE = CONNECTION_TYPES::PLOTTING;
      //CONNECTION_TYPE = CONNECTION_TYPES::COMPUTER;
      if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
        conf_slow_FR_1();
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      }
      run_arr_type1(3, arr, 0);
      CONNECTION_TYPE = CONNECTION_TYPES::COMPUTER;
      
    }                                                                   
      break;

    case hash("get_par"):
    {      
        uint16_t spec[10];        
        Serial.println(get_PAR(spec));
        Serial.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",spec[0],spec[1],spec[2],spec[3],spec[4],spec[5],spec[6],spec[7],spec[8],spec[9]);      
    }
    break;

    case hash("PAR"):
    {      
        uint16_t spec[10];
        Serial.println(get_PAR(spec) * ambit_calibration_local.spec_coef);
        Serial.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",spec[0],spec[1],spec[2],spec[3],spec[4],spec[5],spec[6],spec[7],spec[8],spec[9]);      
    }
    break;


        case hash("awake"):
    {

      while(1){
        uint16_t spec[10];        
        get_PAR(spec);
      }
      
    }
    break;


  case hash("r"):
     {
      uint8_t a = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t b = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t c = (uint8_t) Serial_Input_Long(",", 10);
      
      uint8_t arr[24] = {a, 0, 1, 0, 0, b, 0, 1, \
                        a, 0, 1, 0, 0, b, c, 1,\
                        a, 0, 1, 0, 0, b, 0, 1};

      CONNECTION_TYPE = CONNECTION_TYPES::AMBYTE;
      //CONNECTION_TYPE = CONNECTION_TYPES::COMPUTER;
      if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
        conf_slow_FR_1();
        adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;
      }

      FLAG_DEICE = true;
      run_arr_type1(3, arr, 0, true);
      FLAG_DEICE = false;
      CONNECTION_TYPE = CONNECTION_TYPES::COMPUTER;
      
    }                                                                   
      break;

      case hash("w"):
     {
      CONNECTION_TYPE = CONNECTION_TYPES::PLOTTING;
      uint8_t length = (uint16_t) Serial_Input_Long(",", 10);
      uint8_t interval = (uint8_t) Serial_Input_Long(",", 10);
      uint8_t act = (uint8_t) Serial_Input_Long(",", 10);
      run_trigger_spacer(length, interval, false, act, true);      
    }                                                                   
      break;        
      
      
      case hash("a"):
     {
      uint8_t a = (uint16_t) Serial_Input_Long(",", 10);
      uint8_t b = (uint16_t) Serial_Input_Long(",", 10);

      as7431_blink(a, b);
      
      
    }                                                                   
      break;  

         
      case hash("aa"):
     {
      uint8_t a = (uint16_t) Serial_Input_Long(",", 10);
      
      for (uint8_t i = 0; i < 4; i++){
        as7431_blink(i, a);
      }

      for (uint8_t i = 0; i < 4; i++){
        as7431_blink(i, a);
      }

      for (uint8_t i = 0; i < 4; i++){
        as7431_blink(i, a);
      }

      for (uint8_t i = 0; i < 4; i++){
        as7431_blink(i, a);
      }

      
    }                                                                   
      break;  


   


      case hash("set_act"):
      {
        float_t a = (float_t) Serial_Input_Double(",", 10);
        preferences.begin("config", false);
        preferences.putFloat("actinic", a);
        preferences.end();
      }                                                                   
      break;

      case hash("set_name"):
      {
        char s[16];

        Serial_Input_Chars(s, ",\n\r", 10, 15);
        preferences.begin("config", false);
        preferences.putString("name", s);
        preferences.end();
      }                                                                  
      break;

      case hash("set_emit"):
      {
        double a = Serial_Input_Double(",", 10);
        preferences.begin("config", false);
        preferences.putDouble("emit", a);
        preferences.end();
      }                                                                   
      break;

      case hash("set_spec"):
      {
          float_t f = (float_t) Serial_Input_Double(",", 10);
          preferences.begin("config", false);
          preferences.putFloat("spec", f);
          preferences.end();
      }
      break;

       case hash("reboot"):
      {
        ESP.restart();
      }
      break;

      case hash("check"):
      {
        check_connections();
      }
      break;
      
    int optic_test();
      case hash("test"):
      {
        optic_test();
      }
      break;

      int optic_test(uint8_t current, uint8_t num_integ, uint8_t lit_offset, uint8_t dark1_offset, uint8_t dark2_offset, uint8_t pulse_offset, uint8_t pulse_duration);
      
      case hash("test1"):
      {
        uint8_t current = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t num_integ = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t lit_offset = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t dark1_offset = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t dark2_offset = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t pulse_offset = (uint8_t) Serial_Input_Long(",", 10);
        uint8_t pulse_duration = (uint8_t) Serial_Input_Long(",", 10);
        optic_test(current, num_integ, lit_offset, dark1_offset, dark2_offset, pulse_offset, pulse_duration);
      }
      break;
    default:
      Serial.println("BAD COMMAND");
    break;

  }
}