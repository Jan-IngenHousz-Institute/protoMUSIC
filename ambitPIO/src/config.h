#ifndef _CONFIG_H
#define _CONFIG_H

#define STF_FLASH_PIN 1
#define BOOT_PIN 9
#define AMBIT_BOOT_IDLE 133

enum CONNECTION_TYPES {
    PLOTTING, 
    AMBYTE, 
    COMPUTER,  
};

extern uint8_t CONNECTION_TYPE;

#endif
