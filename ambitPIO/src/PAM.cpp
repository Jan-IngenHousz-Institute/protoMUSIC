#include "PAM.h"

static const char* TAG = "PAM";
static bool measure_temp = true;
static bool PAM_interrupt(bool, bool);

uint8_t adpd_mode = 0;
adpd_current_config_t adpd_current_config;
adpd_gains_config_t adpd_gains_config;


int serial_read_until(uint8_t target1, uint8_t target2 = 0, uint8_t target3 = 0, uint16_t timeout = 20, bool remove = false);

// use pre-set values
int conf_slow_FR_1(void){
  
  if (adpd_gains_config.init == false) ESP_LOGE(TAG, "Gain preset not initized, use default!");
  if (adpd_current_config.init == false) ESP_LOGE(TAG, "Current preset not initized, use default!");
  return conf_slow_FR_1(adpd_current_config.I620, adpd_current_config.I720, adpd_current_config.IR, adpd_gains_config.Fluo, adpd_gains_config.FluoRef, adpd_gains_config.Sun, adpd_gains_config.Leaf, adpd_gains_config.IR, adpd_gains_config.IRRef);
}


// Slow measurements with 4 time slots
// @param I620 measuring light current (0 - 127)
// @param I730 IR reflection current (0 - 127)
// @param I_FR Far-red treatment current (0 - 127)
// @param G_Fluor fluorescence signal gain (0 - 5)
// @param G_FluorRef fluorescence rerefence gain (0 - 5)
// @param G_Sun Sun facing PD gain (0 - 5)
// @param G_IR Leaf facing PD gain (0 - 5)
// @param G_FR IR reflection signal gain (0 - 5)
// @param G_FRref IR reflection reference gain (0 - 5)
int conf_slow_FR_1(uint8_t I620, uint8_t I730, uint8_t I_FR, uint8_t G_Fluor, uint8_t G_FluorRef, uint8_t G_Sun, uint8_t G_IR, uint8_t G_FR, uint8_t G_FRref){

  // Setup timeslot 1: two ambient light channels, 2 x 3 bytes
  adpd.led_config.driver1_current = 0;
  adpd.led_config.driver2_current = 0;
  adpd.SNR_config.TIA_gain_CH2 = G_IR;       // channel 2: leaf IR reflection
  adpd.SNR_config.TIA_gain_CH1 = G_Sun;      // channel 1: sun vis
  adpd.preset_config_1(0, 4);



  // Setup timeslot 2:  Fluor and Ref channels,  4 x 3 bytes
    // LED 1A = 620nm
  adpd.led_config.driver1_current = I620;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;
  adpd.SNR_config.TIA_gain_CH1 = G_Fluor;
  adpd.SNR_config.TIA_gain_CH2 = G_FluorRef;
  adpd.preset_config_2(1, 1);


  // Setup timeslot 3:  IR leave reflection, 2 x 3 bytes
     // LED 1A = 620nm
  adpd.led_config.driver1_current = 0;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = I730;
  adpd.led_config.led2_channel = LED_A;
  adpd.SNR_config.TIA_gain_CH1 = G_FR;
  adpd.SNR_config.TIA_gain_CH2 = G_FRref;

  adpd.preset_config_3(2, 4);

  // Setup timeslot 4-5-6:  Far-red illumination, 0 data
    // LED 1A = 620nm
  adpd.led_config.driver1_current = 0;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = I_FR;
  adpd.led_config.led2_channel = LED_A;

  //make 6 time slots, ~ 2 ms without repeats, can get 200 repeats ~ 400ms
  adpd.preset_config_4(3);
  adpd.preset_config_4(4);
  adpd.preset_config_4(5);
  adpd.preset_config_4(6);
  adpd.preset_config_4(7);
  adpd.preset_config_4(8);

  adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;

  return 0;
}


uint32_t arr_line_parse_type1(uint8_t* line, uint8_t* num1, uint16_t* num2, uint16_t* num3, uint8_t* num4, uint8_t* num5, uint16_t* data_count){
  // type 1: Must have measurements: Fluor/Ref
  // optional measurements: leaf IR, sun VIs, Reflect/Ref
  // 0 = no points, 1 = same freq, 2 = every 8
  // data_count 0: fluor, 1: ambient, 2: reflection
  uint8_t type = line[0];                 // run type 1 = steady state, 0 = skip, 2 = no ir
  *num1 = line[1];                        // FR on / off
  *num2 = line[3] + (line[2] << 8);       // sample number
  *num3 = line[5] + (line[4] << 8);       // frequency
  *num4 = line[6];                        // actinic
  *num5 = line[7];                        // sub-sampling factor

  if (data_count == NULL) return 0;

  if ((type == 1) || (type == 2)){
    data_count[0] = *num2;
    if (line[7] == 0){ // no ambient
      data_count[1] = 0;
      data_count[2] = 0;
    }else if (line[7] == 1){ // every
      data_count[1] = *num2;
      data_count[2] = *num2;
    }else if (line[7] == 2){ // every 8
      data_count[1] = (*num2) / 8;
      data_count[2] = (*num2) / 8;
    }else{
      data_count[1] = 0;
      data_count[2] = 0;
    }
    if (type == 2) data_count[2] = 0;

    return data_count[0] * 2 + data_count[1] * 2 + data_count[2] * 2;

  }
  return 0;  
}



int run_preprocess_type1(uint8_t length, uint8_t* arr, uint16_t* data_counter){
    uint8_t pc = 0;
    uint8_t _type = 0;
    uint8_t para1, actinic, subsampling = 0;
    uint16_t num_ptx, freq = 0;
    uint16_t _data_counter[4] = {0};

    while (pc < length){
      _type = *(arr + pc * 8);
      if ((_type == 1) || (_type == 2)){
        arr_line_parse_type1(arr + pc * 8, &para1, &num_ptx, &freq, &actinic, &subsampling, _data_counter);
        ESP_LOGV(TAG, "Run type:%d, number:%d, freq: %d, actinic: %d, subfactor %d", _type, num_ptx, freq, actinic, subsampling);
        for (uint8_t i = 0; i < 4; i++) data_counter[i] += _data_counter[i];
      }
      pc += 1;
    }
    return 0;
}

int run_arr_type1(uint8_t length, uint8_t* arr, bool led_persist){
  return run_arr_type1(length, arr, led_persist, false);
}


int run_arr_type1(uint8_t length, uint8_t* arr, bool led_persist, bool allow_interrupt){

  if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
    conf_slow_FR_1();
    ESP_LOGW(TAG, "Run array was not configured!");
  }
  // set to max possible bytes
  uint8_t expected_readout = 8;
  uint8_t expected_readout_bytes = expected_readout * 3;
  const uint8_t num_integration = 1;
  const unsigned int start_t0 = millis();

  // Run protocol preprecess, get storage size
  uint16_t data_count[] = {0, 0, 0, 0};
  if (run_preprocess_type1(length, arr, data_count) == -1) return -1;    // calculate data counts
  ESP_LOGV(TAG, "Sample & Ref: %d, optionals: %d", data_count[0], data_count[1]);

  dataclass *d_env = new dataclass; //
  dataclass *d_fluor = new dataclass; // fluorescence signal
  dataclass *d_fluoRef = new dataclass; // fluorescence reference

  dataclass *d_sun = new dataclass; // sun-side ambient
  dataclass *d_leaf = new dataclass;  // leaf-side ambient
  dataclass *d_730 = new dataclass; // 730nm reflectance signal
  dataclass *d_730Ref = new dataclass;  // 730nm reference


  if (!(d_env->init(512))) return -1;
  if (!( (d_fluor->init(data_count[0])) && (d_fluoRef->init(data_count[0])) )) return -1;
  if (data_count[1] > 0){
    if (!( (d_sun->init(data_count[1])) && (d_leaf->init(data_count[1])) )) return -1;}
  if (data_count[2] > 0){
    if (!( (d_730->init(data_count[2])) && (d_730Ref->init(data_count[2])) )) return -1;
  }


  ESP_LOGV(TAG, "Memory allocation completed");


  // variables for each trace
  uint8_t pc = 0;
  uint8_t _type = 0;
  uint8_t farred, actinic, subsampling = 0;
  uint16_t num_ptx, freq = 0;

  // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint32_t counter, ploter1, ploter2 = 0;
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;
  uint32_t buf_opt[4] = {0};
  uint32_t _tmparr = 0;
  uint8_t _repeats = 1;
  uint32_t light_sleep_time = 1;
  float_t leaf_temp = 0.0;
  unsigned int env_timer1 = millis();
  bool measure_temperature = false;
  bool interrupt_run = false;

  _tmparr = PAM_get_env(4, start_t0);
  d_env->put(_tmparr);
  leaf_temp = (int16_t) (_tmparr & 0xFFFF) / 100.0;


  adpd.STOP();
  while (pc < length){
    if (interrupt_run) break;
    _type = *(arr + pc * 8);   //get line type
    if ((_type == 1) || (_type == 2)){  // all channels
      arr_line_parse_type1((arr + pc * 8), &farred, &num_ptx, &freq, &actinic, &subsampling, NULL);
      adpd.run_freq(freq);
      adpd.clear_fifo();
      light_sleep_time = (1000/freq);
      measure_temperature = (light_sleep_time > 20) && measure_temp && actinic < 50;


      if (_type == 1){ // use IR reflect
        if (farred == 1){ // whether use actinic IR   
          adpd.num_ts(9);
          _repeats = int(400/freq);
          if (freq < 3) _repeats = 250;
          if (_repeats == 0) _repeats = 1;
          for (uint8_t i = 3; i < 9;i++) adpd.repeats_only(i, 1, _repeats);
        }
        else{
          adpd.num_ts(3);
        }
        expected_readout = 8;
        expected_readout_bytes = expected_readout * 3;
      }else{ // NO IR
        adpd.num_ts(2);
        expected_readout = 6;
        expected_readout_bytes = expected_readout * 3;
      }

      if (actinic > 3){
        AS_LED_Current(actinic);
        AS_LED_ON();
      }else{
        AS_LED_OFF();
        AS_LED_Current(0);
      }


      counter = 0;
      for (uint8_t i = 0; i < 4; i++) buf_opt[i] = 0;
      adpd.RUN();
      delay(2);
      while (counter < num_ptx){
        if (interrupt_run) break;
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){ // read all bytes from FIFO
          adpd.readfifo(expected_readout, 3, ret);
          fifo_c -= expected_readout_bytes;
          if (counter == num_ptx) break;
          // 0: sun-vis; 1: leaf-ir; 2: fluoS_dark; 3: fluoS_lit; 4: fluoR_dark; 5: fluoR_lit; 6: Reflect_signal; 7: reflect_ref
          // save fluor signal and ref
          d_fluor->put(calc_signal(ret[2], ret[3], num_integration));       
          d_fluoRef->put(calc_signal(ret[4], ret[5], num_integration));


          // save option data
          if (subsampling > 0){
            if (subsampling == 1){ // every point
              d_sun->put(ret[0] > 65000 ? ret[0] - 65000 : 0);
              d_leaf->put(ret[1] > 65000 ? ret[1] - 65000 : 0);
              if (_type == 1){d_730->put(ret[6]);d_730Ref->put(ret[7]);} // ir enabled

            }else if (subsampling == 2){
              buf_opt[0] += (ret[0] > 65000 ? ret[0] - 65000 : 0);
              buf_opt[1] += (ret[1] > 65000 ? ret[1] - 65000 : 0);
              if (_type == 1){
                buf_opt[2] += ret[6];
                buf_opt[3] += ret[7];
              }

              if (counter % 8 == 7){
                d_sun->put(buf_opt[0]/8);
                d_leaf->put(buf_opt[1]/8);
                if(_type == 1){d_730->put(buf_opt[2]/8);d_730Ref->put(buf_opt[3]/8);}                
                for (uint8_t i = 0; i < 4; i++) buf_opt[i] = 0;
              }
            }
          }

          if (CONNECTION_TYPE == CONNECTION_TYPES::PLOTTING){
            ploter1 = calc_signal(ret[2], ret[3], num_integration);
            ploter2 = calc_signal(ret[4], ret[5], num_integration);
            if (ploter2 == 0){
              ploter2 = 1;
              ploter1 = 0;
            }
            if (_type == 1) {
              Serial.printf("T:%2.3f,F:%3.4f,S:%d,R:%d,7:%d,7R:%d,Sun:%d,L:%d\n", leaf_temp, (float)ploter1/(float)ploter2, ploter1, ploter2, ret[6], ret[7], ret[0]-65000, ret[1]-65000);
            }else if (_type == 2){
              Serial.printf("T:%2.3f,F:%3.4f,S:%d,R:%d,Sun:%d,L:%d\n", leaf_temp, (float)ploter1/(float)ploter2, ploter1, ploter2, ret[0]-65000, ret[1]-65000);            }
            Serial.flush();
          }
          counter++;
          watch_dog_timer = 0;
        }

        // do light sleep
        //esp_sleep_enable_timer_wakeup(1000);
        if (counter + 10 < num_ptx){  // a lot of measurements
          // do temperature measurement?
          if (measure_temperature && (millis() - env_timer1 > 2000)){
            _tmparr = PAM_get_env(4, start_t0);
            d_env->put(_tmparr);
            leaf_temp = (int16_t) (_tmparr & 0xFFFF) / 100.0;
            env_timer1 = millis();
            esp_sleep_enable_timer_wakeup(1000);
          }
          else esp_sleep_enable_timer_wakeup(light_sleep_time * 8000);

        }else if (counter + 2 < num_ptx){ // not many 
          esp_sleep_enable_timer_wakeup(light_sleep_time * 1000);
        }else{
          esp_sleep_enable_timer_wakeup(1000);
        }
        // run interrupted by serial input "S"
        interrupt_run = PAM_interrupt(allow_interrupt, false);
        if (!interrupt_run) esp_light_sleep_start();
        interrupt_run = PAM_interrupt(allow_interrupt, true);
      }
      
      adpd.STOP();
      if (!led_persist) AS_LED_OFF();
      digitalWrite(1, LOW);
    }
    pc += 1;
  }

  if (interrupt_run){
    digitalWrite(STF_FLASH_PIN, LOW);
    adpd.STOP();
    AS_LED_OFF();
  };


  if (CONNECTION_TYPE == CONNECTION_TYPES::COMPUTER){
    d_env->send_serial("ENV");
    d_fluor->send_serial("Fluo");
    d_fluoRef->send_serial("Fluoref");
    d_sun->send_serial("SUN");
    d_leaf->send_serial("leaf");
    d_730->send_serial("730");
    d_730Ref->send_serial("730ref");

    Serial.println("Data sent");
  }else if(CONNECTION_TYPE == CONNECTION_TYPES::AMBYTE){
    int ret = d_env->fsm_send_esp(0, allow_interrupt);
    if (ret != 1) d_env->fsm_send_esp(0, allow_interrupt);
    
    d_fluor->fsm_send_esp(1);
    d_fluoRef->fsm_send_esp(2);
    if (subsampling > 0){
      d_sun->fsm_send_esp(3);
      d_leaf->fsm_send_esp(4);
      if (data_count[2] > 0){
        d_730->fsm_send_esp(5);
        d_730Ref->fsm_send_esp(6);
      }
    }
  }

  delete d_fluor;
  delete d_fluoRef;
  delete d_sun;
  delete d_leaf;
  delete d_730;
  delete d_730Ref;
  delete d_env;

  return 0;

}


void adpd_trigger(void){
    digitalWrite(10, HIGH);
    delayMicroseconds(1);
    digitalWrite(10, LOW);
}



int run_trigger_spacer(uint16_t length, uint8_t interval, bool change_act, uint8_t act, bool interrrupt){

  if (length > 3000) return -1;
  adpd.STOP();

  if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1) conf_slow_FR_1();

  digitalWrite(10, LOW);
  adpd.gpio_config.GPIO0_cfg = 1;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 1;
  adpd.gpio_setup(&(adpd.gpio_config));
  adpd_mode = ADPD_CONFIG_MODE::ARRAY_SLOW;

  gpio_sleep_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
  gpio_sleep_set_pull_mode(GPIO_NUM_10, GPIO_PULLDOWN_ONLY);
  


  // set to max possible bytes
  uint8_t expected_readout = 8;
  uint8_t expected_readout_bytes = expected_readout * 3;
  const uint32_t _wait_time_ms = interval * 100;
  const uint8_t num_integration = 1;
  const unsigned int start_t0 = millis();

    // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;
  uint32_t buf_opt[4] = {0};
  uint32_t _tmparr = 0;
  uint32_t read_fluor, read_fluoRef, read_sun, read_leaf, read_7, read_7Ref;
  float_t leaf_temp = 0.0;

  unsigned int env_timer1 = millis(), trigger_timer = 0, expected_millis = 0;
  int waiting_time = 0;
  bool measure_temperature = false;
  bool interrupt_run = false;
  esp_sleep_enable_timer_wakeup(90000);

  dataclass *d_env = new dataclass; //
  dataclass *d_fluor = new dataclass; // fluorescence signal
  dataclass *d_fluoRef = new dataclass; // fluorescence reference

  dataclass *d_sun = new dataclass; // sun-side ambient
  dataclass *d_leaf = new dataclass;  // leaf-side ambient
  dataclass *d_730 = new dataclass; // 730nm reflectance signal
  dataclass *d_730Ref = new dataclass;  // 730nm reference

  int _func_ret = -1;
  if (!(d_env->init(512))) goto del_classes;
  if (!( (d_fluor->init(length)) && (d_fluoRef->init(length)) )) goto del_classes;
  if (!( (d_sun->init(length)) && (d_leaf->init(length)) )) goto del_classes;
  if (!( (d_730->init(length)) && (d_730Ref->init(length)) )) goto del_classes;
  

  _tmparr = PAM_get_env(4, start_t0);
  d_env->put(_tmparr);
  leaf_temp = (int16_t) (_tmparr & 0xFFFF) / 100.0;
  env_timer1 = millis();


  adpd.clear_fifo();
  adpd.run_freq(10);
  adpd.num_ts(3);
  adpd.RUN();
  delay(5);

  if(change_act){
    if (act == 0) AS_LED_OFF();
    if (act > 0) {
      AS_LED_Current(act);
      AS_LED_ON();
      }
  }
  
  for (uint16_t n = 0; n < length; n++){
    fifo_c = 0;
    adpd_trigger();
    trigger_timer = millis();
    expected_millis = trigger_timer + _wait_time_ms;
    delay(1);
    while (fifo_c != expected_readout_bytes){
      fifo_c = adpd.fifo_count();
      if (fifo_c >= expected_readout_bytes) break;
      if (millis() - trigger_timer > 100) break;
    }
    if (fifo_c < expected_readout_bytes){
      ESP_LOGE(TAG, "NOT ENOUGH IN FIFO");
      break;
    }
    adpd.readfifo(expected_readout, 3, ret);
    if (fifo_c > expected_readout_bytes){
      adpd.clear_fifo();
      ESP_LOGE(TAG, "Extra %d byte in FIFO", fifo_c - expected_readout_bytes);
    }

    read_fluor = calc_signal(ret[2], ret[3], num_integration); d_fluor->put(read_fluor);
    read_fluoRef = calc_signal(ret[4], ret[5], num_integration); d_fluoRef->put(read_fluoRef);
    read_sun = ret[0]; d_sun->put(read_sun > 65000 ? read_sun - 65000 : 0);
    read_leaf = ret[1]; d_leaf->put(read_leaf > 65000 ? read_leaf - 65000 : 0);
    read_7 = ret[6]; d_730->put(read_7);
    read_7Ref = ret[7]; d_730Ref->put(read_7Ref);


    if (CONNECTION_TYPE == CONNECTION_TYPES::PLOTTING){
      Serial.printf("T:%2.3f,F:%3.4f,S:%d,R:%d,7:%d,7R:%d,Sun:%d,L:%d\n", leaf_temp, (float)read_fluor/(float)read_fluoRef, read_fluor, read_fluoRef, read_7, read_7Ref, read_sun-65000, read_leaf-65000);    
      Serial.flush();
    }



    if (interrupt_run) break;
    if (millis() > expected_millis) continue; // overdue
    waiting_time = expected_millis - millis();

    if ((n % 8 == 7) && (waiting_time > 100)){
      if (millis() - env_timer1 > 2000){
        _tmparr = PAM_get_env(4, start_t0);
        d_env->put(_tmparr);
        leaf_temp = (int16_t) (_tmparr & 0xFFFF) / 100.0;
        env_timer1 = millis();
      }      
    }    

    

    
    waiting_time = expected_millis - millis();
    while (waiting_time > 250){
      esp_sleep_enable_timer_wakeup((waiting_time - 50) * 1000);
      interrupt_run = PAM_interrupt(interrrupt, false);
      if (!interrupt_run) esp_light_sleep_start();
      interrupt_run = PAM_interrupt(interrrupt, true);
      if (interrupt_run) break;
      waiting_time = expected_millis - millis();
    }
    if (interrupt_run) break;
    waiting_time = expected_millis - millis();
    if (waiting_time > 1) delay(waiting_time);     
  } /// End of Loop


  adpd.STOP();
  adpd.clear_fifo();

  if (CONNECTION_TYPE == CONNECTION_TYPES::AMBYTE){
    int ret = d_env->fsm_send_esp(0, interrrupt);
    if (ret != 1) d_env->fsm_send_esp(0, interrrupt);    
    d_fluor->fsm_send_esp(1);
    d_fluoRef->fsm_send_esp(2);    
    d_sun->fsm_send_esp(3);
    d_leaf->fsm_send_esp(4);    
    d_730->fsm_send_esp(5);
    d_730Ref->fsm_send_esp(6);
  }

  _func_ret = 0;
  del_classes:
    delete d_fluor;
    delete d_fluoRef;
    delete d_sun;
    delete d_leaf;
    delete d_730;
    delete d_730Ref;
    delete d_env;
    
  adpd.gpio_config.GPIO0_cfg = 0;
  adpd.gpio_config.EXT_SYNC_EN = 0;
  adpd.gpio_setup(&(adpd.gpio_config));
  adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;        
  return _func_ret;
}


int external_trigger_run(void){

  adpd.STOP();
  conf_slow_FR_1();

  adpd.led_config.driver1_current = adpd_current_config.I620;
  adpd.led_config.led1_channel = LED_A;
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;
  adpd.SNR_config.TIA_gain_CH1 = adpd_gains_config.Fluo;
  adpd.SNR_config.TIA_gain_CH2 = adpd_gains_config.FluoRef;
  adpd.preset_config_2(1, 4);


  digitalWrite(10, LOW);
  adpd.gpio_config.GPIO0_cfg = 1;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 1;
  adpd.gpio_setup(&(adpd.gpio_config));


  // set to max possible bytes
  uint8_t expected_readout = 6, num_integration = 4;
  uint8_t expected_readout_bytes = expected_readout * 3;
  
    // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint16_t fifo_c = 0;  
  uint32_t read_fluor, read_fluoRef, read_sun, read_leaf;
  float_t leaf_temp = 0.0;

  adpd.clear_fifo();
  adpd.run_freq(10);
  adpd.num_ts(2);
  adpd.RUN();
  delay(5);
  uint8_t unknown_input_counter = 0;

  unsigned int watchdog_timer = millis(), trigger_timer = 0, start_timer = millis(), temp_timer = millis();
  bool keep_running = true, do_measure = false, change_act = false;
  double obj_T, chip_T;
  char c, c1, c2;

  Serial.println("Run");

  while(keep_running){ 
    
    while (Serial.available() == 0){
      if (millis() - watchdog_timer > 30000){
        keep_running = false;
        break;
      }
      delayMicroseconds(200);
    }
    if (!keep_running) break;
    
    while (Serial.available() > 0){
      c = Serial.read();
      if (c == 'G'){
        do_measure = true;
        watchdog_timer = millis();
      }else if(c == 'E'){
        do_measure = false;
        keep_running = false;
      }else if(c == 'A'){
        delay(1);
        c1 = 0;
        if (Serial.available() > 0){
          c1 = Serial.read();
          change_act = true;
        }
      }else if(c == 'T'){
        temp_timer = millis();
        mlx_measure(&obj_T, &chip_T);
        Serial.printf("T:%d,o:%.3f,a:%.3f,d:%d\n", millis() - start_timer, obj_T, chip_T, millis() - temp_timer);
        Serial.flush();
        watchdog_timer = millis();
        continue;
      }
      else{
        unknown_input_counter += 1;
        if (unknown_input_counter > 200) keep_running = false;
      }
    }

    if (!keep_running) break;
    if (change_act){
      change_act = false;
      if (c1 > 3){
        AS_LED_Current(c1);
        AS_LED_ON();
      }else{
        AS_LED_OFF();
      }
    }
    if (!do_measure) continue;


    fifo_c = 0;
    adpd_trigger();
    do_measure = false;
    trigger_timer = millis(); 
    delay(1);
    while (fifo_c != expected_readout_bytes){
      fifo_c = adpd.fifo_count();
      if (fifo_c >= expected_readout_bytes) break;
      if (millis() - trigger_timer > 100) break;
    }
    if (fifo_c < expected_readout_bytes){
      Serial.println("NOT ENOUGH IN FIFO");
      break;
    }
    adpd.readfifo(expected_readout, 3, ret);
    if (fifo_c > expected_readout_bytes){
      adpd.clear_fifo();
      Serial.printf("Extra %d byte in FIFO", fifo_c - expected_readout_bytes);
    }

    read_fluor = calc_signal(ret[2], ret[3], num_integration); 
    read_fluoRef = calc_signal(ret[4], ret[5], num_integration); 
    read_sun = ret[0]; 
    read_leaf = ret[1]; 



    Serial.printf("T:%d,S:%d,R:%d,F:%d,B:%d\n", millis() - start_timer, read_fluor, read_fluoRef, read_sun-65000, read_leaf-65000);    
    Serial.flush();    
  } /// End of Loop


  adpd.STOP();
  adpd.clear_fifo();
    
  adpd.gpio_config.GPIO0_cfg = 0;
  adpd.gpio_config.EXT_SYNC_EN = 0;
  adpd.gpio_setup(&(adpd.gpio_config));

  AS_LED_Current(0);
  AS_LED_OFF();

  Serial.println("Stop");

  return 0;
}


int external_trigger_run_Flash(unsigned int gate_time, unsigned int dt, const uint16_t num){
  adpd.STOP();
  const uint8_t _NUM_TS = 8;

  adpd.led_config.driver1_current = 80;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;
  adpd.SNR_config.TIA_gain_CH1 = 1;
  adpd.SNR_config.TIA_gain_CH2 = 5;
  
  for (uint8_t i = 0; i < 12; i++){
    adpd.preset_config_ext_fast(i, 2);
  }

  digitalWrite(STF_FLASH_PIN, LOW);
  digitalWrite(10, LOW);
  adpd.gpio_config.GPIO0_cfg = 1;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 1;
  adpd.gpio_setup(&(adpd.gpio_config));
  AS_LED_OFF();
  AS_LED_Current(dt);


  // set to max possible bytes
  uint8_t expected_readout = _NUM_TS * 4, num_integration = 2;
  uint8_t expected_readout_bytes = expected_readout * 3;
  
    // data counter and buffer

  uint32_t ret[expected_readout] = {0};
  uint16_t fifo_c = 0;  
  uint32_t read_fluor, read_fluoRef, read_sun, read_leaf;
  float_t leaf_temp = 0.0;

  adpd.clear_fifo();
  adpd.run_freq(10);
  adpd.num_ts(_NUM_TS);
  adpd.RUN();
  delay(5);
  uint16_t _counter = 0;

  unsigned int watchdog_timer = millis(), trigger_timer = 0, start_timer = millis(), temp_timer = millis();
  bool keep_running = true, do_measure = false, change_act = false;
  double obj_T, chip_T;
  char c, c1, c2;

  dataclass *d_fluor = new dataclass; // fluorescence signal
  dataclass *d_fluoRef = new dataclass; // fluorescence reference
  
  

  Serial.println("Run");
  unsigned long timer1 = micros();
  unsigned long timer2 = micros();
  unsigned int flash_duration = expected_readout;
  uint16_t _num_sample = num < 20 ? num * _NUM_TS : 20 * _NUM_TS + (num - 20);



  if (!( (d_fluor->init(_num_sample + 1)) && (d_fluoRef->init(_num_sample + 1)) )) goto del_classes;
  while(keep_running){
    _counter += 1;
    if (_counter > num) break;
    if (!keep_running) break;
    fifo_c = 0;
    //if ((_counter == 5) && (dt > 0)) AS_LED_ON();
    adpd_trigger();    
    timer1 = micros();
    delayMicroseconds(1000);
    fifo_c = adpd.fifo_count();
    while (fifo_c != expected_readout_bytes){
      fifo_c = adpd.fifo_count();
      if (fifo_c >= expected_readout_bytes) break;
      if (micros() - timer1 > 10000) break;
    }
    //Serial.printf("%ld: %d\n", micros() - timer1, fifo_c);

    if (fifo_c < expected_readout_bytes){
      Serial.printf("NOT ENOUGH IN FIFO: %d < %d\n", fifo_c, expected_readout_bytes);
      break;
    }
    

    if ((gate_time > 0) && (flash_duration > 0) && (_counter > 2)) digitalWrite(STF_FLASH_PIN, HIGH);
    //if ((_counter == 15) && (dt > 0)) AS_LED_OFF();

    timer1 = micros();
    for (uint8_t r = 0; r < expected_readout; r++){
      adpd.readfifo(1, 3, ret + r);
      if (flash_duration == r) digitalWrite(STF_FLASH_PIN, LOW);
    }
    //Serial.println(micros() - timer1);
    
    
    if (_counter <= 20){
      for (uint8_t r = 0; r < _NUM_TS; r++){
        d_fluor->put(calc_signal(ret[0 + r * 4], ret[1 + r * 4], num_integration));
        d_fluoRef->put(calc_signal(ret[2 + r * 4], ret[3 + r * 4], num_integration));
      }
    }else{
      read_fluor = 0; read_fluoRef = 0;
      for (uint8_t r = 0; r < _NUM_TS; r++){
        read_fluor += calc_signal(ret[0 + r * 4], ret[1 + r * 4], num_integration);
        read_fluoRef += calc_signal(ret[2 + r * 4], ret[3 + r * 4], num_integration);
      }
      d_fluor->put(read_fluor / _NUM_TS);
      d_fluoRef->put(read_fluoRef / _NUM_TS);
      if (_counter == 100) flash_duration = 4;
      if (_counter == 300) flash_duration = 2;
      if (_counter == 500) flash_duration = 1;
      if (_counter == 700) flash_duration = 32;
    }


    if (fifo_c > expected_readout_bytes){
      fifo_c = adpd.fifo_count();
      if (fifo_c > 0){
        adpd.clear_fifo();
        digitalWrite(STF_FLASH_PIN, LOW);
        Serial.printf("Extra %d byte in FIFO\n", fifo_c - expected_readout_bytes);
        }      
    }
    
    // flash_duration = gate_time - (micros() - timer1);    
    // if ((flash_duration > 1) && (flash_duration < 500)) delayMicroseconds(flash_duration - 1);
    // digitalWrite(STF_FLASH_PIN, LOW);
    //

    
    //Serial.printf("T:%d,S:%d,R:%d,F:%d,B:%d\n", millis() - start_timer, read_fluor, read_fluoRef, read_sun-65000, read_leaf-65000);    
    // Serial.printf("T:%d,S:%d,R:%d\n", millis() - start_timer, read_fluor, read_fluoRef);    
    // flash_duration = gate_time - (micros() - timer1);
    // if (flash_duration > 2){
    //   digitalWrite(STF_FLASH_PIN, HIGH);
    //   delayMicroseconds(flash_duration - 1);
    // }



    digitalWrite(STF_FLASH_PIN, LOW);
  } /// End of Loop
  //Serial.printf("%f for %dpoints\n", float(micros() - timer2)/num, num);
  AS_LED_OFF();
  digitalWrite(STF_FLASH_PIN, LOW);
  adpd.STOP();
  adpd.clear_fifo();

  
    
  adpd.gpio_config.GPIO0_cfg = 0;
  adpd.gpio_config.EXT_SYNC_EN = 0;
  adpd.gpio_setup(&(adpd.gpio_config));

  AS_LED_Current(0);
  AS_LED_OFF();
  digitalWrite(STF_FLASH_PIN, LOW);

  for (uint16_t n = 2; n < _num_sample; n++){
    Serial.printf("%d,%d,%.4f\n", d_fluor->arr[n], d_fluoRef->arr[n], ((float) d_fluor->arr[n]) / d_fluoRef->arr[n]);
  }
  
  del_classes:
    delete d_fluor;
    delete d_fluoRef;
  

  return 0;
}




int MPF(uint16_t mode, uint16_t dc_current){
  if (adpd_gains_config.init == false) ESP_LOGE(TAG, "Gain preset not initized, use default!");
  if (adpd_current_config.init == false) ESP_LOGE(TAG, "Current preset not initized, use default!");
  return MPF(mode, adpd_current_config.I620, dc_current, adpd_gains_config.Fluo, adpd_gains_config.FluoRef);

}


int MPF(uint16_t mode, uint16_t current, uint16_t dc_current, uint8_t sign_gain, uint8_t ref_gain){

  
  const uint8_t num_integration = 1;
  const uint16_t _data_size = 1080;
  
  ESP_LOGV(TAG, "RUN MPF with mode:%d, pulse current:%d, DC current %d", mode, current, dc_current);

  dataclass *d_fluor = new dataclass; // fluorescence signal
  dataclass *d_fluoRef = new dataclass; // fluorescence reference
  if (!( (d_fluor->init(_data_size)) && (d_fluoRef->init(_data_size)) )) return -1;
  ESP_LOGV(TAG, "Memory allocation completed");


  adpd.STOP();
  adpd.run_freq(10);
  adpd.clear_fifo();
  adpd.gpio_config.GPIO0_cfg = 1;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 1;
  adpd.gpio_setup(&(adpd.gpio_config));

  adpd.led_config.driver1_current = current;
  adpd.led_config.driver2_current = 0;
  adpd.SNR_config.TIA_gain_CH2 = ref_gain;
  adpd.SNR_config.TIA_gain_CH1 = sign_gain;

  adpd_mode = ADPD_CONFIG_MODE::MPF_MODE;


   // data buffers
  uint32_t ret[48] = {0};
  uint16_t fifo_c = 0;
  int32_t tmp_var = 0;
  uint32_t avg_arr1[12] = {0};
  uint32_t avg_buf[4] = {0};
  uint16_t as_current = 255;
  uint8_t expected_readout = 12 * 4;    // 12 timeslots, 4 data per ts
  uint16_t decay_interval = 1;
  // PHASE 0----------------------------------------------
  // Apply a baseline without actinic
  // fixed at 2 Hz x 20 pts
  if (mode == 0){
    AS_LED_OFF();
    AS_LED_Current(as_current);
    adpd.preset_config_ext_fast(0);
    expected_readout = 4;
    adpd.RUN();
    delayMicroseconds(1500);
    adpd_trigger();
    delayMicroseconds(1500);
    ESP_LOGV(TAG, "Phase-0 Started");
    for (uint8_t i = 0; i < 20; i++){
      adpd_trigger(); 
      adpd.readfifo(expected_readout, 3, ret); 
      for (uint8_t m = 0; m < 4; m++){    // sign-dark, sign-lit, ref-dark, ref lit
        avg_buf[m] = ret[m];
      }
      d_fluor->put(calc_signal(avg_buf[0], avg_buf[1], num_integration));
      d_fluoRef->put(calc_signal(avg_buf[2], avg_buf[3], num_integration));
      delay(500);
    }
    ESP_LOGV(TAG, "Phase-0 Completed");
  }

  
  // PHASE 1---------------------------------------------------
  // 12 timeslots
  // Rapid induction, all timeslot saved
  ESP_LOGV(TAG, "Phase-1 Config");
  expected_readout = 48;
  for (uint8_t i = 0; i < 12; i++){
    adpd.preset_config_ext_fast(i);
  }
  adpd.RUN();
  // AS Light kept until this point
  AS_LED_OFF();
  AS_LED_Current(0);
  delay(2);
  ESP_LOGV(TAG, "Phase-1 LED ON");
  // Light ON
  AS_LED_ON();
  delayMicroseconds(1);  
  adpd_trigger();
  delayMicroseconds(1500);

  for (uint8_t i = 0; i < 4; i++){
    adpd_trigger(); 
    adpd.readfifo(expected_readout, 3, ret); 
    for (uint8_t j = 0; j < 12; j++){

      d_fluor->put(calc_signal(ret[0 + j * 4], ret[1 + j * 4], num_integration));
      d_fluoRef->put(calc_signal(ret[2 + j * 4], ret[3 + j * 4], num_integration));
    }
    delayMicroseconds(500);
  }
  ESP_LOGV(TAG, "Phase-1 Completed; Phase-2 start");

  // PHASE 2------------------------------------
  // 300ms induction, 200x12ts, 200 final pts
  for (uint8_t i = 0; i < 200; i++){
    adpd_trigger(); 
    adpd.readfifo(expected_readout, 3, ret); 
    // reset avg arrays
    for (uint8_t m = 0; m < 4; m++){    // find median for sign-dark, sign-lit, ref-dark, ref lit
      memset(avg_arr1, 0, sizeof(avg_arr1));
      for (uint8_t n = 0; n < 12; n++){   // put 12 ts into sorting array
        sorted_insert(avg_arr1, 12, ret[m + n * 4]);
      }
      avg_buf[m] = (avg_arr1[5] + avg_arr1[6] + avg_arr1[7]) / 3; // pick the middle 3 numbers and average
    }


    // Serial.printf("P2, %d, %d, %d\n", avg_buf[0], avg_buf[1], calc_signal(avg_buf[0], avg_buf[1], num_integration));
    // Serial.printf("P2, %d, %d, %d\n", avg_buf[2], avg_buf[3], calc_signal(avg_buf[2], avg_buf[3], num_integration));

   
    d_fluor->put(calc_signal(avg_buf[0], avg_buf[1], num_integration));
    // Serial.println(d_fluor->arr[d_fluor->write_ptr - 1]);
    d_fluoRef->put(calc_signal(avg_buf[2], avg_buf[3], num_integration));
    // Serial.println(d_fluoRef->arr[d_fluoRef->write_ptr - 1]);
    delayMicroseconds(500);
  }

  ESP_LOGV(TAG, "Phase-2 Completed; Phase-3 Start");


  // PHASE 3------------------------------------
  // 150ms induction X cycles, 100x12ts, 50 final pts
  for (uint8_t j = 0; j < 8; j++){
    AS_LED_Current(220 - j * 30);
    ESP_LOGV(TAG, "LED set to %d", 220 - j * 30);
    memset(avg_buf, 0, sizeof(avg_buf));
    for (uint8_t i = 0; i < 40; i++){
      adpd_trigger(); 
      adpd.readfifo(expected_readout, 3, ret);
      // reset avg arrays
      for (uint8_t m = 0; m < 4; m++){    // find median for sign-dark, sign-lit, ref-dark, ref lit
        memset(avg_arr1, 0, sizeof(avg_arr1));
        for (uint8_t n = 0; n < 12; n++){   // put 12 ts into sorting array
          sorted_insert(avg_arr1, 12, ret[m + n * 4]);
        }
        avg_buf[m] += (avg_arr1[5] + avg_arr1[6] + avg_arr1[7]) / 3; // pick the middle 3 numbers and average
      }

      if (i % 2 == 1){
        d_fluor->put(calc_signal(avg_buf[0] / 2, avg_buf[1] / 2, num_integration));
        d_fluoRef->put(calc_signal(avg_buf[2] / 2, avg_buf[3] / 2, num_integration));
        memset(avg_buf, 0, sizeof(avg_buf));
      }   
      delayMicroseconds(500);
    }    
  }

  ESP_LOGV(TAG, "Phase-3 Completed; Phase-4 Start");
  // PHASE 4------------------------------------
  AS_LED_Current(as_current);
  // prepare for single timeslot decay kinetic
  expected_readout = 4;
  adpd.preset_config_ext_fast(0);
  adpd.clear_fifo();
  adpd.RUN();
  delayMicroseconds(1000);
  adpd_trigger();
  delayMicroseconds(1500);
  memset(avg_buf, 0, sizeof(avg_buf));

  for (uint8_t i = 0; i < 200; i++){
    adpd_trigger(); 
    adpd.readfifo(expected_readout, 3, ret); 
    for (uint8_t m = 0; m < 4; m++){    // sign-dark, sign-lit, ref-dark, ref lit
      avg_buf[m] += ret[m];
    }
    if (i % 4 == 3){



      // Serial.printf("P4, %d, %d, %d\n", avg_buf[0]/4, avg_buf[1]/4, calc_signal(avg_buf[0]/4, avg_buf[1]/4, num_integration));
      // Serial.printf("P4, %d, %d, %d\n", avg_buf[2]/4, avg_buf[3]/4, calc_signal(avg_buf[2]/4, avg_buf[3]/4, num_integration));


      d_fluor->put(calc_signal(avg_buf[0] / 4, avg_buf[1] / 4, num_integration));
      // Serial.println(d_fluor->arr[d_fluor->write_ptr - 1]);
      d_fluoRef->put(calc_signal(avg_buf[2] / 4, avg_buf[3] / 4, num_integration));
      // Serial.println(d_fluoRef->arr[d_fluoRef->write_ptr - 1]);
      memset(avg_buf, 0, sizeof(avg_buf));
    }
    if (i == 149) {
      if (mode == 0) AS_LED_OFF();        // dark mode, actinic OFF
      if (mode == 1) AS_LED_Current(dc_current);  // light mode, actinic set
    }
    delayMicroseconds(1400);
  }

  ESP_LOGV(TAG, "Phase-4 Completed");
// PHASE -1 ---------------------------
// Dark decay with increasing interval
  if (mode == 0){
    ESP_LOGV(TAG, "Phase-minus1 Started");
    AS_LED_OFF();
    for (uint8_t i = 0; i < 160; i++){
      adpd_trigger(); 
      adpd.readfifo(expected_readout, 3, ret); 
      for (uint8_t m = 0; m < 4; m++){    // sign-dark, sign-lit, ref-dark, ref lit
        avg_buf[m] += ret[m];
      }
      if (i % 4 == 3){
        d_fluor->put(calc_signal(avg_buf[0] / 4, avg_buf[1] / 4, num_integration));
        d_fluoRef->put(calc_signal(avg_buf[2] / 4, avg_buf[3] / 4, num_integration));
        memset(avg_buf, 0, sizeof(avg_buf));
        decay_interval += 5;
      }
      delayMicroseconds(1400);
      delay(decay_interval);
    }
  }

  ESP_LOGV(TAG, "Measurement Completed");
  // Completed

  adpd.STOP();

  if (CONNECTION_TYPE == CONNECTION_TYPES::PLOTTING){
    uint16_t l = d_fluor->get_length();
    uint32_t ploter1, ploter2;
    for (uint16_t i = 0; i < l; i++){
      ploter1 = d_fluor->pop();
      ploter2 = d_fluoRef->pop();
      Serial.printf("F:%3.4f,S:%d,R:%d\n", (float)ploter1/(float)ploter2, ploter1, ploter2);      
    }   
  }else if (CONNECTION_TYPE == CONNECTION_TYPES::AMBYTE){
    d_fluor->fsm_send_esp(0);
    d_fluoRef->fsm_send_esp(1);
  }
  else{
    d_fluor->send_serial("Fluo");
    d_fluoRef->send_serial("Fluoref");
    Serial.println("Data sent");
  }

  ESP_LOGV(TAG, "All Completed");


  adpd.gpio_config.GPIO0_cfg = 0;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 0;
  adpd.gpio_setup(&(adpd.gpio_config));
  
  delete d_fluor;
  delete d_fluoRef;
  return 0;
}


/* Leaf temperature as centi-degC (signed int16 in the low 16 bits). The old
 * time/type bit-packing is gone: env arrays sent to the ambyte are now a plain
 * centi-degC series. Absolute time comes from the ambyte RTC; within-run point
 * timing from the run freq. Only mode 4 (leaf temp) is emitted now; mode/t0 are
 * kept so call sites are unchanged. */
uint32_t PAM_get_env(uint8_t mode, unsigned int t0){
  (void) t0;
  if (mode == 4){
    int16_t centi = (int16_t) (mlx_measure() * 100.0);
    return (uint32_t) (uint16_t) centi;
  }
  return 0;
}

uint32_t PAM_retrieve_env(uint32_t r, uint8_t* mode, float_t* data_f, int16_t* data_i){
  int16_t data = (int16_t) (r & 0x00000FFF);
  uint8_t d_type = (uint8_t) ((r & 0x0000F000) >> 12);
  uint32_t t = ((r & 0xFFFF0000) >> 10);

  if (d_type < 4){
    t += data;
    if (mode != NULL) *mode = d_type;
  }else if (d_type == 4){ // temperature
    if (data_f != NULL) *data_f = (data / 20.0 - 20);
    if (mode != NULL) *mode = d_type;  }
  
  
  return t;
}





int sandbox(uint8_t I620, uint8_t g1, uint8_t g2){

  
  // variables for each trace

  // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[16] = {0};
  uint16_t fifo_c = 0;
  uint16_t fifo_c1 = 0;


  adpd.STOP();
  adpd.gpio_config.GPIO0_cfg = 1;
  adpd.gpio_config.SYNC_GPIO = 0;
  adpd.gpio_config.EXT_SYNC_EN = 1;
  adpd.gpio_setup(&(adpd.gpio_config));

  adpd.led_config.driver1_current = I620;
  adpd.led_config.driver2_current = 0;
  adpd.SNR_config.TIA_gain_CH2 = 5;
  adpd.SNR_config.TIA_gain_CH1 = 1;

  adpd.preset_config_ext_fast(0, 1);
  adpd.preset_config_ext_fast(1, 2);
  adpd.preset_config_ext_fast(2, 3);
  adpd.preset_config_ext_fast(3, 4);
  
  adpd.run_freq(10);
  adpd.clear_fifo();

  adpd.RUN();
  delay(1);
  int64_t timer = 0;


  for (uint16_t i = 0; i < 200; i++){
    digitalWrite(10, HIGH);
    delayMicroseconds(1);
    digitalWrite(10, LOW);
    timer = esp_timer_get_time();
    if (i > 0){
      
      adpd.readfifo(16, 3, ret);

      Serial.printf("Data:%d,%d,%d,%d,", ret[0], ret[1], ret[2],ret[3]);
      Serial.printf("%d,%d,%d,%d,", ret[4], ret[5], ret[6],ret[7]);
      Serial.printf("%d,%d,%d,%d,", ret[8], ret[9], ret[10],ret[11]);
      Serial.printf("%d,%d,%d,%d\n", ret[12], ret[13], ret[14],ret[15]);

      
      delay(100);
      
    }else{
      delayMicroseconds(1500);
    }
    //Serial.println(esp_timer_get_time() - timer);

  }
  //Serial.println(adpd.fifo_count());
  adpd.readfifo(16, 3, ret);

  adpd.STOP();   


  return 0;

}



static bool PAM_interrupt(bool enable, bool check_sleep){
  uint8_t ret = 0;
  if (!enable) return false;
  if (check_sleep){ //  after light sleep
    if (esp_sleep_get_wakeup_cause() == 8){ //  wake up serial
      Serial.flush();
      ret = serial_read_until(177, 0, 0, 25, true);
    }
  }else{
    ret = serial_read_until(177, 0, 0, 15, true);
  }
  if (ret == 1) return true;
  return false;
}


void actinic_test(uint8_t act1, uint8_t act2, uint8_t t1, uint8_t t2){
  AS_LED_Current(act1);
  AS_LED_ON();
  delay(t1 * 1000);
  AS_LED_Current(act2);
  delay(3000);
  AS_LED_OFF();            
  AS_LED_Current(0);
  

}




int fluor_offset_test(uint8_t current, uint8_t num_integ, uint8_t lit_offset, uint8_t dark1_offset, uint8_t dark2_offset, uint8_t pulse_offset, uint8_t pulse_duration){

    // Setup timeslot 2:  Fluor and Ref channels,  4 x 3 bytes
    // LED 1A = 620nm
  adpd.led_config.driver1_current = current;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;
  adpd.SNR_config.TIA_gain_CH1 = 1;
  adpd.SNR_config.TIA_gain_CH2 = 5;
  adpd.preset_config_2x(0, num_integ, lit_offset, dark1_offset, dark2_offset, pulse_offset, pulse_duration);

  adpd_mode = ADPD_CONFIG_MODE::ARRAY_MODE1;

  return 0;
}


int fluor_offset(uint32_t* fret){

  if (adpd_mode != ADPD_CONFIG_MODE::ARRAY_MODE1){
    conf_slow_FR_1();
    ESP_LOGW(TAG, "Run array was not configured!");
  }
  // set to max possible bytes
  uint8_t expected_readout = 8;
  uint8_t expected_readout_bytes = expected_readout * 3;
  const uint8_t num_integration = 1;
  const unsigned int start_t0 = millis();
 

  // variables for each trace
  uint8_t pc = 0;
  uint8_t _type = 0;
  uint8_t farred, actinic, subsampling = 0;
  uint16_t num_ptx = 0;

  // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint32_t counter = 0;
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;
  uint32_t buf_opt[4] = {0};
  uint32_t _tmparr = 0;
  uint8_t _repeats = 1;



  adpd.STOP();
  adpd.run_freq(100);
  adpd.clear_fifo();
  adpd.num_ts(3);
  expected_readout = 8;
  expected_readout_bytes = expected_readout * 3;
  AS_LED_OFF();
  AS_LED_Current(0);

 
  num_ptx = 64;

  uint32_t fluor, fluoRef, sun, leaf, r730, r730Ref;
  uint32_t ret_fluor = 0, ret_fluoRef = 0, ret_sun = 0, ret_leaf = 0, ret_r730 = 0, ret_r730Ref = 0;

  for (uint8_t n = 0; n < 4; n++){
    adpd.RUN();
    delay(1);
    fluor = 0; fluoRef = 0; sun = 0; leaf = 0; r730 = 0; r730Ref = 0;
    counter = 0;

    while (counter < num_ptx){
      fifo_c = adpd.fifo_count();
      while (fifo_c >= expected_readout_bytes){ // read all bytes from FIFO
        adpd.readfifo(expected_readout, 3, ret);
        fifo_c -= expected_readout_bytes;
        if (counter == num_ptx) break;
        // 0: sun-vis; 1: leaf-ir; 2: fluoS_dark; 3: fluoS_lit; 4: fluoR_dark; 5: fluoR_lit; 6: Reflect_signal; 7: reflect_ref
        // save fluor signal and ref
        fluor += calc_signal(ret[2], ret[3], num_integration);       
        fluoRef += calc_signal(ret[4], ret[5], num_integration);
        r730 += ret[6];
        r730Ref += ret[7];
        sun += ret[0];
        leaf += ret[1];
        counter += 1;
      }
      
    }
    adpd.STOP();

    if (counter > 10){
      ret_fluor += fluor / counter;
      ret_fluoRef += fluoRef / counter;
      ret_sun += sun / counter;
      ret_leaf += leaf / counter;
      ret_r730 += r730 / counter;
      ret_r730Ref += r730Ref / counter;
    }
    
  }
  fret[0] = ret_fluor / 4;
  fret[1] = ret_fluoRef / 4;
  fret[2] = ret_sun / 4;
  fret[3] = ret_leaf / 4;
  fret[4] = ret_r730 / 4;
  fret[5] = ret_r730Ref / 4;
  return 0;

}























/*



//

int detector_preset_1(uint8_t current, uint8_t gain_fluo, uint8_t gain_ref, uint8_t gain_par_ir, uint8_t gain_par_vis){

  // Setup timeslot 1: two ambient light channels
  adpd.led_config.driver1_current = 0;
  adpd.led_config.driver2_current = 0;
  adpd.SNR_config.TIA_gain_CH2 = gain_par_ir;
  adpd.SNR_config.TIA_gain_CH1 = gain_par_vis;
  adpd.preset_config_1(0, 4);

  // Setup timeslot 2:  Fluor and Ref channels
    // LED 1A = 620nm
  adpd.led_config.driver1_current = current;
  adpd.led_config.led1_channel = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;

  adpd.SNR_config.TIA_gain_CH1 = gain_fluo;
  adpd.SNR_config.TIA_gain_CH2 = gain_ref;

  adpd.preset_config_2(1, 4);
  ESP_LOGI(TAG, "Preset 1 set");
  return 0;
}






int run_preprocess(uint8_t length, uint8_t* arr, uint16_t* data_counter){
    uint8_t pc = 0;
    uint8_t _type = 0;
    uint8_t para1, actinic, subsampling = 0;
    uint16_t num_ptx, freq = 0;
    uint16_t _data_counter[4] = {0};

    while (pc < length){
      _type = *(arr + pc * 8);
      if (_type == 1){
        arr_line_parse_type1(arr + pc * 8, &para1, &num_ptx, &freq, &actinic, &subsampling, _data_counter);
        ESP_LOGV(TAG, "Run type:%d, number:%d, freq: %d, actinic: %d, subfactor %d", _type, num_ptx, freq, actinic, subsampling);
        for (uint8_t i = 0; i < 4; i++) data_counter[i] += _data_counter[i];
      }
      pc += 1;
    }
    return 0;
}

int send_binary_data(uint32_t* arr, uint16_t len){
  char c;
  Serial.setTimeout(100);
  while(Serial.available()){
    c = Serial.read();
    Serial.print(c);
  }
  
  Serial.println("Wake!");
  
  if (Serial.read("Ready", 5)){
    while(Serial.available()){
      c = Serial.read();
      Serial.print(c);
    }
  }else{
    Serial.println("No response");
  }
}


int run_arr(uint8_t length, uint8_t* arr){ // old version
  const uint8_t expected_readout = 6;
  const uint8_t expected_readout_bytes = expected_readout * 3;
  const uint8_t num_integration = 4;

  // Run protocol preprecess, get storage size
  uint16_t data_count[] = {0, 0, 0, 0};
  if (run_preprocess(length, arr, data_count) == -1) return -1;    // calculate data counts
  ESP_LOGV(TAG, "Sample: %d, ref: %d, amb: %d, dark: %d", data_count[0], data_count[1], data_count[2], data_count[3]);

  dataclass *Fdata = new dataclass;
  dataclass *Rdata = new dataclass;
  dataclass *Adata = new dataclass;
  dataclass *Ddata = new dataclass;

  if (!(Fdata->init(data_count[0]) && Rdata->init(data_count[1]) && Adata->init(data_count[2]) && Ddata->init(data_count[3]))) return -1;
  ESP_LOGV(TAG, "Memory allocation completed");

  // configure the timeslots
  //detector_preset_1(100, 1, 4, 4, 4);

  // variables for each trace
  uint8_t pc = 0;
  uint8_t _type = 0;
  uint8_t para1, actinic, subsampling = 0;
  uint16_t num_ptx, freq = 0;

  // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint32_t counter = 0;
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;  
  uint32_t send_arr[200];


  adpd.STOP();
  while (pc < length){
    _type = *(arr + pc * 8);
    if (_type == 1){
      arr_line_parse_type1((arr + pc * 8), &para1, &num_ptx, &freq, &actinic, &subsampling, data_count);
      adpd.run_freq(freq);
      adpd.clear_fifo();
      if (actinic > 4){
        if (actinic == 255){
          AS_LED_Current(255);
          AS_LED_ON();
          digitalWrite(1, HIGH);
        }
        AS_LED_Current(actinic - 1);
        AS_LED_ON();
      }else{
        AS_LED_OFF();
      }

      counter = 0;
      adpd.RUN();

      while (counter < num_ptx){
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){
          adpd.readfifo(expected_readout, 3, ret);
          fifo_c -= expected_readout_bytes;
          if (counter == num_ptx) break;
          tmp_var = ((int)ret[3] - (int)ret[2] + 250) - (0.006 / (int)num_integration) * ((int)ret[2] - 16384 * num_integration);
          if ((tmp_var > 0) && (ret[3] > ret[2])){
            Fdata->put(tmp_var);
          }else{
            Fdata->put(0);
          }

          //  Serial.printf("%d, %d, %d, %d, %d, %d\n", ret[0], ret[1], ret[2], ret[3], ret[4], ret[5]);
          
          Rdata->put(ret[5] - ret[4]);
          Adata->put(ret[0]);
          Ddata->put(ret[1]);
          counter++;
          watch_dog_timer = 0;
        }

        if (Fdata->length > 4){
          for (uint8_t z = 0; z < 4; z++){
            send_arr[0 + z * 4] = Fdata->pop();
            send_arr[1 + z * 4] = Rdata->pop();
            send_arr[2 + z * 4] = Adata->pop();
            send_arr[3 + z * 4] = Ddata->pop();
            Serial.printf("%d, %d, %d, %d\n", send_arr[0 + z * 4], send_arr[1 + z * 4], send_arr[2 + z * 4],send_arr[3 + z * 4]);
          }
          
          //send_data(send_arr, 16);
        }else{
        // esp_sleep_enable_timer_wakeup(wait_time * 5000);
        // esp_light_sleep_start();
          delay(1);
        }
        
      }
      
      adpd.STOP();
      AS_LED_OFF();
      digitalWrite(1, LOW);
    }    
    if (_type == 2){
      //flash duration
      para1 = *(arr + pc * 8 + 6);
      digitalWrite(1, HIGH);
      delay(para1);
      digitalWrite(1, LOW);
    }
    pc += 1;
  }

  delete Ddata;
  delete Adata;
  delete Rdata;
  delete Fdata;

  return 0;

}

*/

/*

int data_allocation(void** F_data, void **R_data, void **A_data, void **D_data, uint16_t* data_count){

  if ((data_count[0] + data_count[1] + data_count[2] + data_count[3]) > MAX_MEMORY_ALLOC){
    ESP_LOGE(TAG, "More than %d", MAX_MEMORY_ALLOC*4);
    return -1;
  }


  if ((data_count[0] > 0) and data_count[0] < MAX_MEMORY_ALLOC) *F_data = heap_caps_calloc(data_count[0], sizeof(uint32_t), MALLOC_CAP_32BIT);
  if (*F_data == NULL) goto F_data_failed;
  ESP_LOGV(TAG, "Allocation F_data: %d bytes", data_count[0] * 4);

  if ((data_count[1] > 0) and data_count[1] < MAX_MEMORY_ALLOC) *R_data = heap_caps_calloc(data_count[1], sizeof(uint32_t), MALLOC_CAP_32BIT);
  if (*R_data == NULL) goto R_data_failed;
  ESP_LOGV(TAG, "Allocation R_data: %d bytes", data_count[1] * 4);

  if ((data_count[2] > 0) and data_count[2] < MAX_MEMORY_ALLOC) *A_data = heap_caps_calloc(data_count[2], sizeof(uint32_t), MALLOC_CAP_32BIT);
  if (*A_data == NULL) goto A_data_failed;
  ESP_LOGV(TAG, "Allocation A_data: %d bytes", data_count[2] * 4);

  if ((data_count[3] > 0) and data_count[3] < MAX_MEMORY_ALLOC) *D_data = heap_caps_calloc(data_count[3], sizeof(uint32_t), MALLOC_CAP_32BIT);
  if (*D_data == NULL) goto D_data_failed;
  ESP_LOGV(TAG, "Allocation D_data: %d bytes", data_count[3] * 4);

  

  return 0;

  D_data_failed:
  free(D_data);
  A_data_failed:
  free(A_data);
  R_data_failed:
  free(R_data);
  F_data_failed:
  free(F_data);
  ESP_LOGE(TAG, "Allocation failed");
  return -1;
}





int run_arr(uint8_t length, uint8_t* arr){ // old version
  const uint8_t expected_readout = 6;
  const uint8_t expected_readout_bytes = expected_readout * 3;
  const uint8_t num_integration = 4;

  // Run protocol preprecess, get storage size
  uint16_t data_count[] = {0, 0, 0, 0};
  if (run_preprocess(length, arr, data_count) == -1) return -1;    // calculate data counts
  ESP_LOGV(TAG, "Sample: %d, ref: %d, amb: %d, dark: %d", data_count[0], data_count[1], data_count[2], data_count[3]);
  uint32_t *F_data = NULL;
  uint32_t *R_data = NULL;
  uint32_t *A_data = NULL;
  uint32_t *D_data = NULL;
  if (data_allocation((void**)(&F_data), (void**)&R_data, (void**)&A_data, (void**)&D_data, data_count) == -1) return -1;
  F_data[0] = 0;
  ESP_LOGV(TAG, "Memory allocation completed");

  // configure the timeslots
  //detector_preset_1(100, 1, 4, 4, 4);

  // variables for each trace
  uint8_t pc = 0;
  uint8_t _type = 0;
  uint8_t para1, actinic, subsampling = 0;
  uint16_t num_ptx, freq = 0;

  // data counter and buffer
  // [sun-amb, leaf-ir, lit_leaf-ir, dark_leaf-ir, lit_leaf-ref, dark_leaf-ref]
  uint32_t ret[expected_readout] = {0};
  uint32_t counter = 0;
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;  


  adpd.STOP();
  while (pc < length){
    _type = *(arr + pc * 8);
    if (_type == 1){
      arr_line_parse_type1((arr + pc * 8), &para1, &num_ptx, &freq, &actinic, &subsampling, data_count);
      adpd.run_freq(freq);
      adpd.clear_fifo();
      if (actinic > 4){
        AS_LED_Current(actinic);
        AS_LED_ON();
      }else{
        AS_LED_OFF();
      }
      counter = 0;
      adpd.RUN();

      while (counter < num_ptx){
        fifo_c = adpd.fifo_count();
        while (fifo_c >= expected_readout_bytes){
          adpd.readfifo(expected_readout, 3, ret);
          fifo_c -= expected_readout_bytes;
          if (counter == num_ptx) break;
          tmp_var = ((int)ret[3] - (int)ret[2] + 250) - (0.006 / (int)num_integration) * ((int)ret[2] - 16384 * num_integration);
          F_data[counter] = 0;
          if ((tmp_var > 0) && (ret[3] > ret[2])) F_data[counter] = tmp_var;
          R_data[counter] = ret[5] - ret[4];
          A_data[counter] = ret[0];
          D_data[counter] = ret[1];

          if (true){
            Serial.print(F_data[counter]);
            Serial.print(",");
            Serial.print(R_data[counter]);
            Serial.print(",");
            Serial.print(A_data[counter]);
            Serial.print(",");
            Serial.print(D_data[counter]);
            Serial.println();
          }
          counter++;
          watch_dog_timer = 0;
        }

        // esp_sleep_enable_timer_wakeup(wait_time * 5000);
        // esp_light_sleep_start();
        delay(1);    
      }
      adpd.STOP();
      AS_LED_OFF();
    }    
    pc += 1;
  }



  free(D_data);
  free(A_data);
  free(R_data);
  free(F_data);
  
  return 0;


}






int steady_state(uint8_t current, uint8_t gain_fluo, uint8_t gain_ref, uint8_t gain_dark, uint16_t freq, uint16_t num, uint16_t extra_info1, uint16_t extra_info2){

  const uint8_t num_integration = 4;

  uint32_t *F_data = NULL;
  uint32_t *R_data = NULL;
  uint32_t *A_data = NULL;
  

  uint16_t wait_time = 1000 / freq;
  if (wait_time > 1000) wait_time = 1000;
  
  F_data = (uint32_t*)heap_caps_calloc(num, sizeof *F_data, MALLOC_CAP_32BIT);
  if (F_data == NULL) return -8;
  R_data = (uint32_t*)heap_caps_calloc(num, sizeof *R_data, MALLOC_CAP_32BIT);
  if (R_data == NULL){
    free(F_data);
    return -9;
  };
  A_data = (uint32_t*)heap_caps_calloc(num, sizeof *A_data, MALLOC_CAP_32BIT);
  if (A_data == NULL){
    free(F_data);
    free(R_data);
    return -10;
  };
  


    // LED 1A = 620nm
  adpd.led_config.driver1_current = 40;
  adpd.led_config.driver1_current = LED_A;
    // LED 2A = 730nm
  adpd.led_config.driver2_current = 0;
  adpd.led_config.led2_channel = LED_A;

  
  //adpd.SNR_config.TIA_gain_CH1 = gain_dark;

  adpd.led_config.driver2_current = current;
  adpd.SNR_config.TIA_gain_CH1 = gain_fluo;
  adpd.SNR_config.TIA_gain_CH2 = gain_ref;
  adpd.preset_config_2(0, num_integration);


  adpd.run_freq(freq);
  adpd.clear_fifo();
  //AS_LED_OFF();

  unsigned long start_time = millis(); 

  adpd.RUN();
  uint32_t ret[6] = {0};
  uint32_t counter = 0;
  uint16_t fifo_c = 0;
  uint8_t watch_dog_timer = 0;
  int32_t tmp_var = 0;

  
  while (counter < num){
    fifo_c = adpd.fifo_count();
    while (fifo_c > 14){
      adpd.readfifo(5, 3, ret);
      if (counter == num) break;

      tmp_var = (ret[2] - ret[1] + 250) - (0.006 / num_integration) * ((int)ret[1] - 16384 * num_integration);

      F_data[counter] = 0;
      if (tmp_var > 0) F_data[counter] = tmp_var;
      R_data[counter] = ret[4] - ret[3];
      A_data[counter] = ret[0];


      if (true){
        Serial.print(int((float)F_data[counter] / R_data[counter] * 1000));
        Serial.print(",");
        Serial.print(F_data[counter]);
        Serial.print(",");
        Serial.print(R_data[counter]);
        Serial.print(",");
        Serial.println(A_data[counter]);
      }

      // if (printout){
      //   Serial.print(ret[2] - 16384 * num_integration + 1000);
      //   Serial.print(",");
      //   Serial.print(ret[1] - 16384 * num_integration + 1000);
      //   Serial.print(",");
      //   Serial.print(tmp_var);
      //   Serial.print(",");
      //   Serial.println(A_data[counter]);
      // }


      counter++;
      watch_dog_timer = 0;
      fifo_c -= 15;
    }

    // esp_sleep_enable_timer_wakeup(wait_time * 5000);
    // esp_light_sleep_start();
    delay(wait_time);
    watch_dog_timer += 1;
    if (watch_dog_timer > 4){
      Serial.println("RUN timeout");
      break;
    }
    
  }
  adpd.STOP();

  
  
  free(F_data);
  free(R_data);
  free(A_data);

  if (watch_dog_timer > 4) return -8;

  return 1;
}
*/
