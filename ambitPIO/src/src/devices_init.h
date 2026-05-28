#ifndef __DEVINIT_H__
#define __DEVINIT_H__

#include "pin_config.h"

#include "Wire.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include <Arduino.h>

#define DEBUGGING 1

extern bool SPI_bus_initialized;
extern bool I2C_bus_initialized;

bool init_spi_bus(void);
bool init_i2c_bus(void);
void i2c_scan(void);
// bool i2c_scan(uint8_t);
// void miniPorQ_air (int pumps, int pump_voltage);




#endif