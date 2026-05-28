#ifndef __u_MLX_H_
#define __u_MLX_H_

#include <Arduino.h>
#include <Adafruit_I2CDevice.h>
#include "../pin_config.h"
#include "mlx_dev.h"

int32_t mlx90632_i2c_read(int16_t register_address, uint16_t *value);
int16_t mlx90632_i2c_read16(int16_t register_address);
int32_t mlx90632_i2c_read32(int16_t register_address);
int32_t mlx90632_i2c_write(int16_t register_address, uint16_t value);
extern bool FLAG_DEICE;

void usleep(int min_range, int max_range);
void msleep(int msecs);


bool mlx_init(void);
double mlx_measure(double* object, double* ambient);
double mlx_measure(double* object, double* ambient, double* reflect_obj, int16_t* a1, int16_t* a2, int16_t* a3, int16_t* a4);
double mlx_measure();

void mlx_print_paras(double e);
void mlx_read_coe(int32_t*);




#endif