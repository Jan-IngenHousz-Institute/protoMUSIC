#ifndef _DU_H
#define _DU_H

#include <Arduino.h>
#include "config.h"


#define MAX_DATACLASS_SIZE 2000
#define WAKE_AMBYTE 211
#define AMBYTE_AWAKE 210
#define AMBYTE_CALLS 170
#define AMBYTE_CALLFORRESET 222
#define AMBYTE_INTR 177

#define AMBYTE_READY_FOR_ARRAY 200
#define AMBYTE_DATA_PASS 180

#define ERR_CHECKSUM_FAILED -10
#define ERR_TOO_MANY_WKUP -4
#define ERR_TOO_MANY_RETRY -9
#define ERR_LOST_SYNC -2
#define ERR_NO_DATA_REQUEST -5

enum DATA_STATUS {  
    WAKEUPCALLS,
    LENGTHARRAY,
    SENDDATA,
    COMPLETED
};


class dataclass{

    public:
    uint32_t *arr = NULL;           // data array handle
    uint16_t write_ptr = 0;         // pointer to next input location
    uint16_t read_ptr = 0;          // pointer to next read location
    uint16_t peek_ptr = 0;          // pointer to next read location
    bool available = false;         // indicator of memory allocation
    bool write_available = false;   // available to add data
    bool read_available = false;    // available to read data
    uint16_t length = 0;
    uint8_t data_fsm_state = DATA_STATUS::WAKEUPCALLS;
    
    uint16_t num_retry = 0;




    dataclass();
    ~dataclass();

    void clean(void);
    void clear(void);
    void put(uint32_t data);
    uint16_t get_length(void);
    bool pop(uint32_t* data);
    uint32_t pop(void);
    uint32_t send(void (*func) (uint32_t*, uint16_t), uint8_t*);
    bool init(uint16_t length);
    void print_all();
    void send_serial(const char*);

    //-- FSM--//
    int fsm_wake_up_calls(void);
    int fsm_wake_up_calls(bool);
    int fsm_send_length_info(uint8_t arr_idx);
    int fsm_send_data(void);
    int fsm_send_esp(uint8_t arr_idx);
    int fsm_send_esp(uint8_t arr_idx, bool interrupt);
    int fsm_send_waitesp();
    //int send_esp(uint8_t);


    private:
    bool loop_ahead = false;
    uint16_t _overwritten_counter = 0;
    uint16_t _length = 0;            // size of data inside
    uint16_t _num_wake_up_calls = 0;
    uint16_t _num_checksum_err = 0;

};

void sorted_insert(uint32_t arr[], uint16_t length, uint32_t c);
uint32_t calc_signal(uint32_t, uint32_t, uint8_t);


#endif