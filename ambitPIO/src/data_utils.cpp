#include "data_utils.h"


static const char* TAG = "DATA_UTIL";
extern bool FLAG_DEICE;


// return 1, 2, 3
// or -1
int serial_read_until(uint8_t target1, uint8_t target2 = 0, uint8_t target3 = 0, uint16_t timeout = 20, bool remove = false){
    unsigned long start_t = millis();
    uint8_t counter, b = 0;

    while (((millis() - start_t) < timeout)){
        if (Serial.available() > 0){
            b = Serial.peek();
            if (b == target1){
                if (remove) Serial.read();
                return 1;
            } else if ((target2 > 0) && (b == target2)){
                if (remove) Serial.read();
                return 2;
            } else if ((target3 > 0) && (b == target3)){
                if (remove) Serial.read();
                return 3;
            }
        
            b = Serial.read();            
            if (b > 127){
                Serial.print(b);
            }else{
                Serial.write(b);
            }            
        }
        else{
            delayMicroseconds(10);
        }
    }
    return -1;
}

uint16_t flush_serial(uint8_t timeout){    
    uint16_t c = 0;
    uint8_t r = 0;
    delay(1);
    unsigned long start_t = millis();
    if (Serial.available() < 1) return 0;

    while (((millis() - start_t) < timeout)){
        if (Serial.available() > 0){
            r = Serial.read();
            if (r < 127) {
                Serial.write(r);
            }else{
                Serial.print(r);
            }            
            c += 1;
        }else{
            break;
        }
    }
    return c;
}



dataclass::dataclass(){
    dataclass::available = false;
    dataclass::write_available = false;
    dataclass::read_available = false;
    ESP_LOGV(TAG, "DATACLASS Created");
    return;
};

dataclass::~dataclass(void){
    ESP_LOGV(TAG, "DATACLASS destroied");    
    if (dataclass::available) dataclass:clean();  
    return;
};

void dataclass::clean(void){
    if (dataclass::available) free((void*) (dataclass::arr));
    ESP_LOGV(TAG, "Memory freed");
    dataclass::arr = NULL;
    dataclass::available = false;
    dataclass::write_available = false;
    dataclass::read_available = false;
    return;
};


bool dataclass::init(uint16_t length){
    if ((length > 0) and length < MAX_DATACLASS_SIZE){
        ESP_LOGV(TAG, "Allocation %d bytes", length * 4);
        (dataclass::arr) = (uint32_t*) heap_caps_calloc(length, sizeof(uint32_t), MALLOC_CAP_32BIT);
        if (dataclass::arr == NULL){
            ESP_LOGE(TAG, "Allocation %d bytes failed", length * 4);
            return false;
        }
        dataclass::_length = length;
        dataclass::available = true;
        dataclass::clear();
        dataclass::write_available = true;
        dataclass::write_ptr = 0;
        dataclass::read_ptr = 0;
        dataclass::read_available = false;
        dataclass::_overwritten_counter = 0;
        dataclass::loop_ahead = false;
        return true;
    }
    else{
        ESP_LOGE(TAG, "Out-of-range: %d bytes ", length * 4);
        return false;
    }    
    return false;
};

// set data array to 0
void dataclass::clear(void){
    if (dataclass::available){
        for(uint16_t i = 0; i < dataclass::_length; i++) (dataclass::arr)[i] = 0;
        dataclass::write_ptr = 0;
        dataclass::read_ptr = 0;
        dataclass::loop_ahead = false;
        ESP_LOGV(TAG, "ARR reset to 0");
    }else{
        ESP_LOGE(TAG,"RESET failed, not initialized");
    }
}

// add the next point to the data array
// update loop_over
void dataclass::put(uint32_t data){
    if (!(dataclass::available)){
        ESP_LOGE(TAG, "Add failed, not initialized");
        return;
    }

    // write point cycling around and reach read pointer
    if ((dataclass::write_ptr >= dataclass::read_ptr) && (dataclass::loop_ahead)){
        dataclass::read_ptr = dataclass::write_ptr + 1;
        dataclass::length -= dataclass::write_ptr - dataclass::read_ptr + 1;
        if (dataclass::read_ptr >= dataclass::_length) dataclass::read_ptr = 0;
        dataclass::_overwritten_counter += 1;
        ESP_LOGW(TAG, "Data over-written, total: %d", dataclass::_overwritten_counter);
    }

    dataclass::arr[write_ptr] = data;
    dataclass::length += 1;
    dataclass::write_ptr += 1;

    dataclass::read_available = true;
    if (dataclass::write_ptr >= dataclass::_length){
        dataclass::write_ptr = 0;
        dataclass::loop_ahead = true;
    }

}

uint16_t dataclass::get_length(void){
    if (!(dataclass::available)){
        ESP_LOGE(TAG, "Add failed, not initialized");
        return 0;
    }
    if (!(dataclass::read_available)){
        ESP_LOGW(TAG, "No data available");
        return 0;
    }
    if (dataclass::loop_ahead){
        if (dataclass::write_ptr <= dataclass::read_ptr) return
                    dataclass::_length - dataclass::read_ptr + dataclass::write_ptr;
        ESP_LOGW(TAG, "write_ptr > read_ptr After loop around!!!!");
        return 0;
    }else{
        if (dataclass::write_ptr > dataclass::read_ptr) return (dataclass::write_ptr - dataclass::read_ptr);
        if (dataclass::write_ptr == dataclass::read_ptr){
            dataclass::read_available = false;
            return 0;
        }
        ESP_LOGW(TAG, "write_ptr < read_ptr WITHOUT loop around!!!!");
        return 0;
    }
    return 0;  // should not reach here
}



/* Sum of the 4 little-endian bytes of v. The data checksum is a byte-sum of
 * the payload (matching the bytes send_binary_array transmits and the ambyte's
 * verification) — summing the uint32 value into a uint8 would only cover the
 * low byte. */
static inline uint8_t u32_byte_sum(uint32_t v)
{
    return (uint8_t)v + (uint8_t)(v >> 8) + (uint8_t)(v >> 16) + (uint8_t)(v >> 24);
}

uint32_t dataclass::send(void (*func) (uint32_t*, uint16_t), uint8_t* c){
    uint8_t checksum = 0;
    if (dataclass::get_length() == 0){
        dataclass::read_available = false;
        ESP_LOGW(TAG, "No data available");
        return 0;
    }

    if (dataclass::loop_ahead){
        if (dataclass::write_ptr <= dataclass::read_ptr){
            func(this->arr + this->read_ptr, this->_length - this->read_ptr);
            func(this->arr, this->read_ptr);
            for (uint16_t i = 0; i < this->_length - this->read_ptr; i++) checksum += u32_byte_sum(this->arr[this->read_ptr + i]);
            for (uint16_t i = 0; i < this->read_ptr; i++) checksum += u32_byte_sum(this->arr[i]);
            *c = checksum;
            return 1;
        }
        ESP_LOGW(TAG, "write_ptr > read_ptr After loop around!!!!");
        return 0;
    }else{
        if (dataclass::write_ptr > dataclass::read_ptr){
            func(this->arr + this->read_ptr, this->write_ptr - this->read_ptr);
            for (uint16_t i = 0; i < this->write_ptr - this->read_ptr; i++) checksum += u32_byte_sum(this->arr[this->read_ptr + i]);
            *c = checksum;
            return 1;}

        if (dataclass::write_ptr == dataclass::read_ptr){
            dataclass::read_available = false;
            return 0;
        }
        ESP_LOGW(TAG, "write_ptr < read_ptr WITHOUT loop around!!!!");
        return 0;
    }
    return 0;   
}

bool dataclass::pop(uint32_t* data){
    if (!(dataclass::available)){
        ESP_LOGE(TAG, "Add failed, not initialized");
        return false;
    }
    if (!(dataclass::read_available)){
        ESP_LOGW(TAG, "No data available");
        return false;
    }
    
    if (dataclass::get_length() == 0){
        dataclass::read_available = false;
        ESP_LOGW(TAG, "No data available");
        return false;
    }

    if (dataclass::read_ptr >= dataclass::_length){
        ESP_LOGE(TAG, "Read_pointer outside range");
        return false;
    }

    *data = dataclass::arr[dataclass::read_ptr];
    dataclass::read_ptr += 1;
    dataclass::length -= 1;
    if (dataclass::read_ptr >= dataclass::_length){
        dataclass::read_ptr = 0;
        dataclass::loop_ahead = false;
    }
    return true;   
}

uint32_t dataclass::pop(void){
    uint32_t a;
    if (dataclass::pop(&a)) return a;
    return 0xABCDEF01;
}

void dataclass::send_serial(const char* tag){

    if (!this->available) return; 
    uint16_t tmp_var = dataclass::get_length();
    Serial.printf("Data:%s,Length:%d\t", tag, tmp_var);
    if (tmp_var == 0){
        Serial.println();
        return;
    }
    for (uint16_t i = 0; i < tmp_var; i++){
        Serial.printf("%d,", dataclass::pop());
    }
    Serial.print("\n");
    return;
}

static void send_binary_array(uint32_t* arr, uint16_t len){
    Serial.write((uint8_t*) arr, len * 4);
}

int dataclass::fsm_wake_up_calls(bool interrupt){
    if (this->data_fsm_state != DATA_STATUS::WAKEUPCALLS) return -1;

    unsigned int timer1 = millis();
    int ret = -1;
    uint8_t wake_up_reason, cmd_wait_time;
    int16_t wake_call_counter = 0;
    int16_t max_wake_try = 2000;
    cmd_wait_time = 15;
    flush_serial(10);
    // sending wake up calls, first 10 in 10ms, remaining
    while (wake_call_counter < max_wake_try){
        Serial.write(WAKE_AMBYTE);
        Serial.flush();
        ret = serial_read_until(AMBYTE_AWAKE, AMBYTE_CALLS, AMBYTE_CALLFORRESET, cmd_wait_time, false);
        if (ret > 0) break;
        // after first 10 fast tries, pulse every second with light sleep
        if (wake_call_counter == 10){
            esp_sleep_enable_timer_wakeup(1000000); //  set up light sleep timer
            cmd_wait_time = 100;
        }
        if (wake_call_counter > 10){
            wake_up_reason = 0;

           
            if (!FLAG_DEICE){  // Power saving mode
                esp_light_sleep_start();
                wake_up_reason = esp_sleep_get_wakeup_cause();
            }else{ // Power wasting mode
                for (uint8_t j = 0; j < 10; j++){
                    ret = serial_read_until(AMBYTE_AWAKE, AMBYTE_CALLS, AMBYTE_CALLFORRESET, 100, false);
                    if (ret > 0) break; 
                }
            }

            if (wake_up_reason == 8){ // wake up by uart
                max_wake_try -= wake_call_counter;
                wake_call_counter = 0;
            }


        }
        wake_call_counter += 1;
        if (millis() - timer1 > 3600000) break;
    }

    if (ret == 1){ // Normal
        this->data_fsm_state = DATA_STATUS::LENGTHARRAY;
        ESP_LOGV(TAG, "Ambyte awake in %d ms with %d tries", millis() - timer1, this->_num_wake_up_calls);
        return 1;
    }else if (ret == 2){ // Lost sync
        ESP_LOGE(TAG, "ERR_LOST_SYNC");
        Serial.read();
        return ERR_LOST_SYNC;
    }else if (ret == 3){
        ESP_LOGE(TAG, "Ambyte calls for re-start");
        Serial.read();
        return 0;
    }else{ // no response, re-try until timeout
        return ERR_NO_DATA_REQUEST;
    }
}


int dataclass::fsm_wake_up_calls(void){
    if (this->data_fsm_state != DATA_STATUS::WAKEUPCALLS) return -1;
    if (this->_num_wake_up_calls > 20) return ERR_TOO_MANY_WKUP;
    if (this->num_retry > 18) return ERR_TOO_MANY_RETRY;


    unsigned int timer1 = millis();
    this->_num_wake_up_calls += 1;
    this->num_retry += 1;

    flush_serial(10); // remove serial buffer

    // potential status
    //1. Normal: run finished, ambyte sleep
    //2. Ambyte 170 CMD: Lost_sync, return to Idle
    //3. Ambyte 222 reset: Re-do the data send. Re-try + 1
    
    Serial.write(WAKE_AMBYTE);  // send run-finished, data-ready code
    int ret = serial_read_until(AMBYTE_AWAKE, AMBYTE_CALLS, AMBYTE_CALLFORRESET, 100, false);
    if (ret == 1){ // Normal
        this->data_fsm_state = DATA_STATUS::LENGTHARRAY;
        ESP_LOGV(TAG, "Ambyte awake in %d ms with %d tries", millis() - timer1, this->_num_wake_up_calls);
        this->_num_wake_up_calls = 0;
        return 0;
    }else if (ret == 2){ // Lost sync
        ESP_LOGE(TAG, "ERR_LOST_SYNC");
        Serial.read();
        return ERR_LOST_SYNC;
    }else if (ret == 3){
        ESP_LOGE(TAG, "Ambyte calls for re-start");
        Serial.read();
        return 0;
    }else{ // no response, re-try until timeout
        return 0;
    }
}

int dataclass::fsm_send_length_info(uint8_t arr_idx){
    if (this->data_fsm_state != DATA_STATUS::LENGTHARRAY) return -1;
    unsigned int timer1 = millis();

    int ret = serial_read_until(AMBYTE_AWAKE, 0, 0, 100, true);
    if (ret != 1){
        ESP_LOGE(TAG, "Status not match: length got %d", ret);
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }
    uint16_t tmp_var = dataclass::get_length();
    uint8_t arr[8] = {212, 150, arr_idx, 0, 0, 0, 0, 0};
    arr[3] = ((tmp_var >> 8) & 0xFF);
    arr[4] = (((tmp_var) & 0xFF));
    for (uint8_t i = 0; i < 7; i++) arr[7] += arr[i];
    flush_serial(5);
    Serial.write(arr, 8);
    ret = serial_read_until(AMBYTE_READY_FOR_ARRAY, 0, AMBYTE_CALLFORRESET, 200, false);
    // ambyte ready for data, no extra byte to send

    if (ret == 1){
        this->data_fsm_state = DATA_STATUS::SENDDATA;
        return 0;
    }else if (ret == 2){
        ESP_LOGE(TAG, "del");
        return 0;
    }else if (ret == 3){
        ESP_LOGE(TAG, "Ambyte calls for re-start");
        Serial.read();
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }else{
        ESP_LOGE(TAG, "UNknown response");
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }
}

int dataclass::fsm_send_data(void){
    if (this->data_fsm_state != DATA_STATUS::SENDDATA) return -1;
    if (this->_num_checksum_err > 6) return ERR_CHECKSUM_FAILED;
    unsigned int timer1 = millis();
    int ret = serial_read_until(AMBYTE_READY_FOR_ARRAY, 0, 0, 100, true);
    if (ret != 1){
        ESP_LOGE(TAG, "Status not match: data");
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }
    uint8_t arr[4] = {212, 0, 0, 0};
    flush_serial(5);
    this->send(send_binary_array, &arr[3]);
    Serial.flush();
    Serial.write(arr, 4);
    Serial.flush();
    this->_num_checksum_err += 1;
    ret = serial_read_until(AMBYTE_DATA_PASS, AMBYTE_READY_FOR_ARRAY, AMBYTE_CALLFORRESET, 500, false);
    if (ret == 1){
        this->data_fsm_state = DATA_STATUS::COMPLETED;
        Serial.read();
        ESP_LOGV(TAG, "Data sent in %d ms", millis() - timer1);
        return 0;
    }else if (ret == 2){
        ESP_LOGE(TAG, "Ambyte asked for resending");
        return 0;
    }else if (ret == 3){
        ESP_LOGE(TAG, "Ambyte calls for re-start");
        Serial.read();
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }
    else{
        ESP_LOGE(TAG, "Unknown response");
        this->data_fsm_state = DATA_STATUS::WAKEUPCALLS;
        return 0;
    }
}

// case 1: interrupt by ambyte, ambyte wait for 212
// case 2: 
int dataclass::fsm_send_waitesp(){
    flush_serial(10);
    
    unsigned int timer = millis();
    uint8_t ret = 0;
    while (millis() - timer < 36000000){
        Serial.write(WAKE_AMBYTE); // write 211
        // wait ambyte ready response 210
        ret = serial_read_until(AMBYTE_AWAKE, AMBYTE_CALLS, AMBYTE_CALLFORRESET, 100, false);
        if (ret == 1) break;

    }

    

}
int dataclass::fsm_send_esp(uint8_t arr_idx){
    return this->fsm_send_esp(arr_idx, false);
}

int dataclass::fsm_send_esp(uint8_t arr_idx, bool use_interrupt){
    if (!this->available) return 1; 
    int ret = 0;
    bool running = true;
    unsigned int timer1 = millis();

    while ((running) && ((millis() - timer1) < 2000)){
        ret = 0;
        switch (this->data_fsm_state)
        {
        case DATA_STATUS::WAKEUPCALLS:
            ret = this->fsm_wake_up_calls(use_interrupt);
            if (ret == 1) timer1 = millis();
            if (ret < 0) return ret;
            break;
        
        case DATA_STATUS::LENGTHARRAY:
            ret = this->fsm_send_length_info(arr_idx);
            if (ret < 0) return ret;
            break;

        case DATA_STATUS::SENDDATA:
            ret = this->fsm_send_data();
            if (ret < 0) return ret;
            break;

        case DATA_STATUS::COMPLETED:
            running = false;
            break;
        default:
            break;
        }
    }
    ESP_LOGV(TAG, "DATA sending succuss in %d ms", millis() - timer1);
    return 1;
}





// int dataclass::send_esp(uint8_t arr_num){
//     int _tmp = 0;
//     uint8_t ambyte_status, target = 0;
//     unsigned int timer1 = 0;
//     uint16_t tmp_var = dataclass::get_length();
//     if (tmp_var == 0) return 0;
    
//     //--- step one, wake up ambyte
//     timer1 = millis();
//     ESP_LOGV(TAG, "Wake up calls");
//     Serial.write(211);
//     for (uint8_t i = 0; i < 25; i++){        
//         Serial.write(211);
//         _tmp = read_until(210, 170, 0, 45, true);
//         if (_tmp == 1){
//             ambyte_status = AMBYTE_STATUS::AWAKE;
//             ESP_LOGV(TAG, "Wake response in %d ms", millis() - timer1);
//             break;
//         }      
//     }

//     if (ambyte_status != AMBYTE_STATUS::AWAKE){
//         ESP_LOGE(TAG, "Awake failed");
//         return -1;
//     }


//     //------------------------------
//     //--- step two, send array type and length
//     // 150, array number, sizeH, sizeL, 0, checksum
//     uint8_t arr[8] = {212, 150, arr_num, 0, 0, 0, 0, 0};
//     arr[3] = ((tmp_var >> 8) & 0xFF);
//     arr[4] = (((tmp_var) & 0xFF));
//     for (uint8_t i = 0; i < 7; i++) arr[7] += arr[i];
    
//     ESP_LOGV(TAG, "Send length");
//     _tmp = 0;
//     for (uint8_t i = 0; i < 25; i++){
//         Serial.write(arr, 8);
//         _tmp = read_until(200, 210, 222, 50, true);
//         if (_tmp == 1){
//             ambyte_status = AMBYTE_STATUS::WAIT_FOR_DATA;
//             break;
//         }else if (_tmp == 2){
//             continue;
//         }else if (_tmp == 3){
//             ESP_LOGE(TAG, "Ambyte askes for restart, TODO");
//             return -1;
//         }

//     }

//     if (ambyte_status != AMBYTE_STATUS::WAIT_FOR_DATA){
//         ESP_LOGE(TAG, "Not available for data");
//         return -1;
//     }

//     //--------------------------------
//     // --- step three, send data
//     // 151, res, res, res 4 bytes array
    
//     for (uint8_t n = 0; n < 3; n++){
//         memset(arr, 0, sizeof(arr));
//         arr[0] = 212;
//         timer1 = millis();
//         this->send(send_binary_array, &arr[3]);
//         Serial.write(arr, 4);
//         ESP_LOGV(TAG, "Spend %d ms sending %d data", millis() - timer1, tmp_var);
//         _tmp = read_until(180, 200, 222, 50, true);
//         Serial.read(); 
//         if (_tmp == 1) return 0;
//         if (_tmp == 2) continue;
//         if (_tmp == -1) return -1;        
//     }

//     return -1;
// }



void dataclass::print_all(void){

    if (!(dataclass::available)){
        ESP_LOGE(TAG, "Add failed, not initialized");
        return;
    }
    for (uint16_t i = 0; i < dataclass::_length; i++){
        Serial.print(dataclass::arr[i]);
        Serial.print(",");
    }
    Serial.println();
    return;
}


// int wait_for_response_clear(const char* s, uint8_t slen, uint8_t timeout){
//     unsigned long timer1 = millis();
//     uint16_t n = 0;
//     Serial.setTimeout(timeout);

//     if (Serial.find(s, slen)){
//         //ESP_LOGI(TAG, "Responsed in %d ms\n",  millis() - timer1);
//         while(Serial.available()){
//             Serial.read();
//             n += 1;
//         }
//         if (n > 0) ESP_LOGW(TAG, "received %d unexpected bytes after %s", n, s);
//         return 0;
//     }
//     return -1;
// }

// bool send_and_wait_rsp(const char *s, const char *r, uint8_t rlen, uint8_t timeout){
//     Serial.println(s);
//     uint8_t i = wait_for_response_clear(r, rlen, timeout);
//     if (i == 0) return true;
//     return false;
// }

// void write32(uint32_t v){
//     uint8_t a = (v) & 0xFF;
//     uint8_t b = ((v) >> 8) & 0xFF;
//     uint8_t c = ((v) >> 16) & 0xFF;
//     uint8_t d = ((v) >> 24) & 0xFF;
//     Serial.write(d);
//     Serial.write(c);
//     Serial.write(b);
//     Serial.write(a);
//     return;
// }

// void send_data(uint32_t* arr, uint16_t len){

//     char c[10];

//     // Wake up sleep device
//     if (send_and_wait_rsp("Wake!", "Ready", 5, 10)){        
//         // send data size
//         sprintf(c, "%d", len);
//         if (send_and_wait_rsp(c, "GO", 2, 10)){
//           uint32_t checksum = 0;
//           for (uint16_t n = 0; n < len; n++){
//             write32((uint32_t) arr[n]);
//             checksum += arr[n];
//           }
//           write32(checksum);          
//           send_and_wait_rsp("DONE", "Check", 5, 10);
//         }               
//        }
// }

/*
    insert a number to an array with order
    large number will insert towards the end
    used for get median
    @param arr: array with sorted data
    @param length: length of the array
    @param c: new data to be inserted
*/
void sorted_insert(uint32_t arr[], uint16_t length, uint32_t c){
  uint16_t n = 0;
  uint16_t nM = length - 1;

  while (n < nM){
    if (arr[n] == 0){
      arr[n] = c;
      break;
    }
    else if (c < arr[n]){
      for (uint8_t g = nM; g > n; g--){
        arr[g] = arr[g - 1];
      }
      arr[n] = c;
      break;
    } else if (arr[n + 1] == 0){
      arr[n + 1] = c;
      break;
    }else{
      n += 1;
    }
  }
  return;
}


uint32_t calc_signal(const uint32_t dark, const uint32_t lit, const uint8_t num){
    if (lit + 250 < dark) return 0;
    uint32_t a = (lit - dark + 250);
    int32_t b = a + 98 - dark * 0.006 / num;

    //int32_t tmp_var = (lit - dark + 250) - (0.006 / p) * (dark - 16384UL * p);
    if (b > 0) return b;
    return 0;
}

