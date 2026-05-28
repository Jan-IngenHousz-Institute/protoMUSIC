#include "spec_meas.h"
#include "Adafruit_AS7341.h"
static const char* TAG = "SPEC";


Adafruit_AS7341 as7341;


bool as7341_setup = false;

bool check_AS7341(){
    if (as7341.begin()) {
        as7341_setup = true;
        return true;
    }
    else{
        ESP_LOGE(TAG, "AS7341 init failed");
        as7341_setup = false;
        return false;
    }
}

void AS_LED_ON(){
    as7341.enableLED(true);
}

void AS_LED_OFF(){
    as7341.enableLED(false);
}

void AS_LED_Current(uint16_t current){
    as7341.setLEDCurrent(current);  // set actinic LED current
    if (current == 0) as7341.enableLED(false);
}

	

void AS_all_channel(uint16_t T1, uint16_t T2, uint16_t *spec){
    if (!as7341_setup){
         check_AS7341();
    }
    //(T1 + 1) * (T2 + 1) * 2.78 / 1000

    as7341.setATIME(T1);
    as7341.setASTEP(T2);
    as7341.setGain(AS7341_GAIN_2X);

    if (!as7341.readAllChannels(spec)){
        Serial.println("Error reading all channels!");
    }    
}

void AS_all_channel(uint16_t T1, uint16_t T2){
    uint16_t spec[12];
    AS_all_channel(T1, T2, spec);
    for (uint8_t i = 0; i < 11; i++){
        Serial.print(spec[i]);
        Serial.print(",");
    }
    Serial.print(spec[11]);
    Serial.println("");
}

// double get_PAR(uint16_t *spec){
//     if (!as7341_setup){
//         check_AS7341();
//     }    
//     double par = 0;
    
//     as7341.setATIME(29);
//     as7341.setASTEP(499);
//     as7341.setGain(AS7341_GAIN_2X);

//     if (!as7341.readAllChannels(spec)){
//         Serial.println("Error reading all channels!");
//         return 0;
//     }
    
//     par = spec[0] + spec[1] + spec[2] + spec[3] +  spec[6] + spec[7] + spec[8] + spec[9]/2;
//     if (par < 100){
//         as7341.setGain(AS7341_GAIN_256X);
//         if (!as7341.readAllChannels(spec)){
//             Serial.println("Error reading all channels!");
//             return 0;
//         }
//         par = par * 128;
//         par = (par + spec[0] + spec[1] + spec[2] + spec[3] +  spec[6] + spec[7] + spec[8] + spec[9]/2)/2;
//     }else{
//         par = par * 128;
//     }
//     return par/36;
// }

// double raw_PAR(uint16_t *spec, as7341_gain_t gain){
//     if (!as7341_setup){
//         check_AS7341();
//     }    
//     double par = 0;

//     as7341.setATIME(29);
//     as7341.setASTEP(499);
//     as7341.setGain(gain);

//     if (!as7341.readAllChannels(spec)){
//         Serial.println("Error reading all channels!");
//         return 0;
//     }

//     uint16_t ir = (spec[5] + spec[11])/16;
//     uint16_t clear = (spec[4] + spec[10])/8;
    
//     par = spec[0] * PAR_COE1 + spec[1] * PAR_COE2 + spec[2] * PAR_COE3 + spec[3] * PAR_COE4 +  spec[6] * PAR_COE5 + spec[7] * PAR_COE6 + \
//             spec[8] * PAR_COE7 + spec[9] * PAR_COE8 + clear * PAR_COE9 + ir * PAR_COE10;
            
//     return par;
// }


// double get_PAR(uint16_t *spec){
//     double par = raw_PAR(spec, AS7341_GAIN_8X);
//     if (par < 200){
//         par = raw_PAR(spec, AS7341_GAIN_256X)/32;
//         for (uint8_t n = 0; n < 12; n++) spec[n] = spec[n]/32;
//     }
//     return par * PAR_OFFSET;    
// }

// double get_PAR(){
//     uint16_t spec[12];
//     return get_PAR(spec);    
// }

void dual_exposure(as7341_gain_t gain1,as7341_gain_t gain2, uint16_t readings_buffer[]){
    if (!as7341_setup){
        check_AS7341();
    }
    

    as7341.setATIME(99);
    as7341.setASTEP(499);
    as7341.setGain(gain1);

    as7341.setSMUXLowChannels(true);        // Configure SMUX to read low channels
    as7341.enableSpectralMeasurement(true); // Start integration
    as7341.delayForData(0);                 // I'll wait for you for all time

    Adafruit_BusIO_Register channel_data_reg =
      Adafruit_BusIO_Register(as7341.i2c_dev, AS7341_CH0_DATA_L, 2);
    bool low_success = channel_data_reg.read((uint8_t *)readings_buffer, 12);

    as7341.setGain(gain2);
    as7341.setSMUXLowChannels(false);        // Configure SMUX to read low channels
    as7341.enableSpectralMeasurement(true); // Start integration
    as7341.delayForData(0);                 // I'll wait for you for all time

     low_success = channel_data_reg.read((uint8_t *)&readings_buffer[6], 12);
}

void cali_PAR(){
    if (!as7341_setup){
        check_AS7341();
    }
    uint16_t spec[12];
    uint16_t calc_spec[10];

    dual_exposure(AS7341_GAIN_8X, AS7341_GAIN_8X, spec);

    calc_spec[0] = spec[0] * Spec_COE1;
    calc_spec[1] = spec[1] * Spec_COE2;
    calc_spec[2] = spec[2] * Spec_COE3;
    calc_spec[3] = spec[3] * Spec_COE4;
    calc_spec[4] = spec[6] * Spec_COE5;
    calc_spec[5] = spec[7] * Spec_COE6;
    calc_spec[6] = spec[8] * Spec_COE7;
    calc_spec[7] = spec[9] * Spec_COE8;
    calc_spec[8] = spec[11] * Spec_COE9;
    calc_spec[9] = spec[10];

    for (uint8_t n = 0; n < 10; n++){
        Serial.print(calc_spec[n]);
        Serial.print(",");
    }

    Serial.println();

    return;
}

double get_PAR(uint16_t *calc_spec){
    if (!as7341_setup){
        check_AS7341();
    }
    uint16_t spec[12];
    float calc_par = 0;

    //uint16_t calc_spec[10];

    dual_exposure(AS7341_GAIN_2X, AS7341_GAIN_2X, spec);

    calc_spec[0] = spec[0] * Spec_COE1;
    calc_spec[1] = spec[1] * Spec_COE2;
    calc_spec[2] = spec[2] * Spec_COE3;
    calc_spec[3] = spec[3] * Spec_COE4;
    calc_spec[4] = spec[6] * Spec_COE5;
    calc_spec[5] = spec[7] * Spec_COE6;
    calc_spec[6] = spec[8] * Spec_COE7;
    calc_spec[7] = spec[9] * Spec_COE8;
    calc_spec[8] = spec[11] * Spec_COE9;
    calc_spec[9] = spec[10];

    for (uint8_t n = 0; n < 8; n++){
        calc_par +=  calc_spec[n];
    }
    calc_par = calc_par * 0.006 - calc_spec[8] * 0.0075;
    return calc_par * PAR_OFFSET;
}


double get_PAR(){
    uint16_t spec[10];
    return get_PAR(spec);
}



uint8_t as7431_reg_write(uint8_t reg, uint8_t data){
    Wire.beginTransmission(AS7341_I2CADDR_DEFAULT);
    Wire.write(reg);
    Wire.write(data);
    return Wire.endTransmission();
}


uint8_t as7431_reg_read(uint8_t reg, uint8_t* data, uint16_t len){
    Wire.beginTransmission(AS7341_I2CADDR_DEFAULT);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)AS7341_I2CADDR_DEFAULT, (uint8_t)len, (uint8_t)0);
    for (uint16_t i = 0; i < len; i++) {
        data[i] = Wire.read();
    }
    return 0;
}


void as7431_blink(uint8_t n, uint8_t intensity){

    while (Serial.available()) Serial.print(Serial.read());

    uint8_t reg = intensity >> 1;
    if (intensity < 4) reg = 1;
    reg |= 0b10000000;

    as7431_reg_write(0xA9, 0b00010000);
    as7431_reg_write(0x70, 0b00001000);
    as7431_reg_write(0x74, 0x00);



    unsigned long timer = millis();
    uint8_t a,b;
    a = 18 - n * 2;
    b = 30 - a;

    while(millis() - timer < 60000){
        for (uint8_t z = 0; z < 128; z++){
            as7431_reg_write(0x74, reg);
            delayMicroseconds(a * 1024);
            as7431_reg_write(0x74, 0x00);
            delayMicroseconds(b * 1024);
        }
        if (Serial.available() > 1) break;
    }
    as7431_reg_write(0x74, 0x00);
    as7431_reg_write(0xA9, 0x0);
    
    return;   
}