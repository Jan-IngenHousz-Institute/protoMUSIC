#include "Adafruit_AS7341.h"
#include "../pin_config.h"

#define PAR_COE1 -0.515
#define PAR_COE2 0.801
#define PAR_COE3 -0.385
#define PAR_COE4 -0.202 
#define PAR_COE5 0.301
#define PAR_COE6 0.183
#define PAR_COE7 0.055
#define PAR_COE8 0.081
#define PAR_COE9 -0.033

#define Spec_COE1 12
#define Spec_COE2 10
#define Spec_COE3 11
#define Spec_COE4 10 
#define Spec_COE5 10
#define Spec_COE6 9
#define Spec_COE7 7
#define Spec_COE8 4
#define Spec_COE9 1

#define PAR_OFFSET 4

extern Adafruit_AS7341 as7341;

extern bool as7341_setup;
bool check_AS7341();
void AS_LED_Current(uint16_t current);
void AS_LED_ON();
void AS_LED_OFF();

void AS_all_channel(uint16_t T1, uint16_t T2, uint16_t *spec);
void AS_all_channel(uint16_t T1, uint16_t T2);
double get_PAR();
double get_PAR(uint16_t *spec);



uint8_t as7431_reg_write(uint8_t reg, uint8_t data);
uint8_t as7431_reg_read(uint8_t reg, uint8_t* data, uint16_t len);
void as7431_blink(uint8_t n, uint8_t intensity);
