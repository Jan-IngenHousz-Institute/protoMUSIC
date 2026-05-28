#include "u_mlx.h"

Adafruit_I2CDevice *mlx = NULL;
static const char* TAG = "MLX";
double mlx_emissivity = 1.0;








int32_t mlx_cali_PR = 0x00587f5b;
int32_t mlx_cali_PG = 0x04a10289;
int32_t mlx_cali_PT = 0xfff966f8;
int32_t mlx_cali_PO = 0x00001e0f;
int32_t mlx_cali_Ea = 4859535;
int32_t mlx_cali_Eb = 5686508;
int32_t mlx_cali_Fa = 53855361;
int32_t mlx_cali_Fb = 42874149;
int32_t mlx_cali_Ga = -14556410;
int16_t mlx_cali_Ha = 16384;
int16_t mlx_cali_Hb = 0;
int16_t mlx_cali_Gb = 9728;
int16_t mlx_cali_Ka = 10752;

 int32_t mlx90632_i2c_read(int16_t register_address, uint16_t *value){
    uint8_t addr[2] = {0, 0};
    uint8_t read_buff[2] = {0, 0};
    addr[0] = (uint8_t)(register_address >> 8);
    addr[1] = (uint8_t)(register_address & 0x00FF);
    mlx->write_then_read(addr, 2, read_buff, 2, false);
    *value = 0;
    *value += (read_buff[0] << 8);
    *value += read_buff[1];
    return 0;
  }

  int16_t mlx90632_i2c_read16(int16_t register_address){
    uint8_t addr[2] = {0, 0};
    uint8_t read_buff[2] = {0, 0};
    int16_t value = 0;
    addr[0] = (uint8_t)(register_address >> 8);
    addr[1] = (uint8_t)(register_address & 0x00FF);
    mlx->write_then_read(addr, 2, read_buff, 2, false);
    value = read_buff[1];
    value |=( (read_buff[0] << 8) & 0xFF00);
    return value;
  }


  int32_t mlx90632_i2c_read32(int16_t register_address){
    uint8_t addr[2] = {0, 0};
    uint8_t readout[4] = {0, 0, 0, 0};
    int32_t value = 0;
    uint16_t n1 = 0;
    uint16_t n2 = 0;
    addr[0] = (uint8_t)(register_address >> 8);
    addr[1] = (uint8_t)(register_address & 0x00FF);

    mlx->write(addr, 2, false);
    mlx->read(readout, 4);  

    n1 += (readout[0] << 8);
    n1 += (readout[1]);

    n2 += (readout[2] << 8);
    n2 += (readout[3]);
    value = n1;
    value |= n2 << 16 & 0xFFFF0000;
    return value;
  }

  int32_t mlx90632_i2c_write(int16_t register_address, uint16_t value){
    uint8_t addr[4] = {0, 0, 0, 0};
    addr[0] = (uint8_t)(register_address >> 8);
    addr[1] = (uint8_t)(register_address & 0x00FF);
    addr[2] = (uint8_t)(value >> 8);
    addr[3] = (uint8_t)(value & 0x00FF);
    mlx->write(addr, 4);
    return 0;
  }

  
void usleep(int min_range, int max_range){
  delayMicroseconds(min_range);
}

void msleep(int msecs){
  delay(msecs);
}



bool mlx_init(void){
    ESP_LOGV(TAG, "MLX90632 initate");
    mlx = new Adafruit_I2CDevice(ADDR_I2C_MLX90632);
    if (!mlx->begin()){
        ESP_LOGE(TAG, "MLX device not found on I2C bus");
        return false;
    }

    if (mlx90632_init() < 0){
      ESP_LOGE(TAG, "MLX device communication failed");
      return false;
    }

    mlx_cali_PR = mlx90632_i2c_read32(0x240C);
    mlx_cali_PG = mlx90632_i2c_read32(0x240E);
    mlx_cali_PT = mlx90632_i2c_read32(0x2410);
    mlx_cali_PO = mlx90632_i2c_read32(0x2412);
    mlx_cali_Ea = mlx90632_i2c_read32(0x2424);
    mlx_cali_Eb = mlx90632_i2c_read32(0x2426);
    mlx_cali_Fa = mlx90632_i2c_read32(0x2428);
    mlx_cali_Fb = mlx90632_i2c_read32(0x242A);
    mlx_cali_Ga = mlx90632_i2c_read32(0x242C);
    mlx_cali_Gb = mlx90632_i2c_read16(0x242E);
    mlx_cali_Ka = mlx90632_i2c_read16(0x242F);
    mlx_cali_Ha = mlx90632_i2c_read16(0x2481);
    mlx_cali_Hb = mlx90632_i2c_read16(0x2482);
    mlx90632_set_refresh_rate(MLX90632_MEAS_HZ_16);

    mlx90632_set_emissivity(mlx_emissivity);

    ESP_LOGI(TAG, "MLX90632 initate OK");

    return true;   

}

double mlx_measure(double* object, double* ambient){

    int32_t ret = 0; /**< Variable will store return values */
    // double ambient; /**< Ambient temperature in degrees Celsius */
    // double object; /**< Object temperature in degrees Celsius */
    int16_t ambient_new_raw = 120;
    int16_t ambient_old_raw = 320;
    int16_t object_new_raw = 3210;
    int16_t object_old_raw = 1230;


    ret = mlx90632_read_temp_raw(&ambient_new_raw, &ambient_old_raw,
                                    &object_new_raw, &object_old_raw);
    *ambient = mlx90632_calc_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_PT, mlx_cali_PR, mlx_cali_PG, mlx_cali_PO, mlx_cali_Gb);
    double pre_ambient = mlx90632_preprocess_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_Gb);
    double pre_object = mlx90632_preprocess_temp_object(object_new_raw, object_old_raw,ambient_new_raw, ambient_old_raw, mlx_cali_Ka);
    *object = mlx90632_calc_temp_object(pre_object, pre_ambient, mlx_cali_Ea, mlx_cali_Eb, mlx_cali_Ga, mlx_cali_Fa, mlx_cali_Fb, mlx_cali_Ha, mlx_cali_Hb);

    //ESP_LOGI(TAG, "MLX90632 Run");
    return *object;
}

double mlx_measure(){
  double object, ambient;
  mlx_measure(&object, &ambient);
  return object;
}

double mlx_measure(double* obj, double* amb, double* obj_r, int16_t* a1, int16_t* a2, int16_t* a3, int16_t* a4){
  int32_t ret = 0; /**< Variable will store return values */
  // double ambient; /**< Ambient temperature in degrees Celsius */
  // double object; /**< Object temperature in degrees Celsius */
  int16_t ambient_new_raw = 120;
  int16_t ambient_old_raw = 320;
  int16_t object_new_raw = 3210;
  int16_t object_old_raw = 1230;
  double pre_ambient, pre_object, ambient, object, obj2;

  ret = mlx90632_read_temp_raw(&ambient_new_raw, &ambient_old_raw, &object_new_raw, &object_old_raw);
  pre_ambient = mlx90632_preprocess_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_Gb);  /// AMB
  pre_object = mlx90632_preprocess_temp_object(object_new_raw, object_old_raw,ambient_new_raw, ambient_old_raw, mlx_cali_Ka); /// ST0
  ambient = mlx90632_calc_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_PT, mlx_cali_PR, mlx_cali_PG, mlx_cali_PO, mlx_cali_Gb);  // Ta
  object = mlx90632_calc_temp_object(pre_object, pre_ambient, mlx_cali_Ea, mlx_cali_Eb, mlx_cali_Ga, mlx_cali_Fa, mlx_cali_Fb, mlx_cali_Ha, mlx_cali_Hb);
  obj2 = mlx90632_calc_temp_object_reflected(pre_object, pre_ambient, ambient, mlx_cali_Ea, mlx_cali_Eb, mlx_cali_Ga, mlx_cali_Fa, mlx_cali_Fb, mlx_cali_Ha, mlx_cali_Hb);

  *obj = object;
  *amb = ambient;
  *obj_r = obj2;
  *a1 = ambient_new_raw;
  *a2 = ambient_old_raw;
  *a3 = object_new_raw;
  *a4 = object_old_raw;
  return obj2;
}


void mlx_print_paras(double e){

  // Serial.printf("%d,%d,%d,%d,%d,%d,%d,%d\n",mlx_cali_PR,mlx_cali_PG,mlx_cali_PT,mlx_cali_PO,mlx_cali_Ea,mlx_cali_Eb,mlx_cali_Fa,mlx_cali_Fb);
  // Serial.printf("%d,%d,%d,%d,%d\n",mlx_cali_Ga,mlx_cali_Ha,mlx_cali_Hb,mlx_cali_Gb,mlx_cali_Ka);

  int32_t ret = 0; /**< Variable will store return values */
  // double ambient; /**< Ambient temperature in degrees Celsius */
  // double object; /**< Object temperature in degrees Celsius */
  int16_t ambient_new_raw = 120;
  int16_t ambient_old_raw = 320;
  int16_t object_new_raw = 3210;
  int16_t object_old_raw = 1230;

  double pre_ambient, pre_object, ambient, object, obj2;
  mlx90632_set_emissivity(e);



  unsigned int timer = millis();
  while (millis() - timer < 25000){
    ret = mlx90632_read_temp_raw(&ambient_new_raw, &ambient_old_raw, &object_new_raw, &object_old_raw);
    pre_ambient = mlx90632_preprocess_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_Gb);  /// AMB
    pre_object = mlx90632_preprocess_temp_object(object_new_raw, object_old_raw,ambient_new_raw, ambient_old_raw, mlx_cali_Ka); /// ST0
    ambient = mlx90632_calc_temp_ambient(ambient_new_raw, ambient_old_raw, mlx_cali_PT, mlx_cali_PR, mlx_cali_PG, mlx_cali_PO, mlx_cali_Gb);  // Ta
    object = mlx90632_calc_temp_object(pre_object, pre_ambient, mlx_cali_Ea, mlx_cali_Eb, mlx_cali_Ga, mlx_cali_Fa, mlx_cali_Fb, mlx_cali_Ha, mlx_cali_Hb);
    obj2 = mlx90632_calc_temp_object_reflected(pre_object, pre_ambient, ambient, mlx_cali_Ea, mlx_cali_Eb, mlx_cali_Ga, mlx_cali_Fa, mlx_cali_Fb, mlx_cali_Ha, mlx_cali_Hb);


    Serial.printf("%d,%d,%d,%d,%f,%f,%f,%f\n",ambient_new_raw,ambient_old_raw,object_new_raw,object_old_raw, pre_ambient, ambient, object,obj2);
  }

  //ret = mlx90632_read_temp_raw(&ambient_new_raw, &ambient_old_raw, &object_new_raw, &object_old_raw);

  
  //Serial.printf("%d,%d,%d,%d,%d\n",ambient_new_raw,ambient_old_raw,object_new_raw,object_old_raw);

  // double object, ambient;
  // mlx_measure(&object, &ambient);

  // Serial.printf("%f, %f\n", object, ambient);

}


void mlx_read_coe(int32_t* arr){
  arr[0] = mlx_cali_PR;
  arr[1] = mlx_cali_PG;
  arr[2] = mlx_cali_PT;
  arr[3] = mlx_cali_PO;
  arr[4] = mlx_cali_Ea;
  arr[5] = mlx_cali_Eb;
  arr[6] = mlx_cali_Fa;
  arr[7] = mlx_cali_Fb;
  arr[8] = mlx_cali_Ga;
  arr[9] = mlx_cali_Ha;
  arr[10] = mlx_cali_Hb;
  arr[11] = mlx_cali_Gb;
  arr[12] = mlx_cali_Ka;
  arr[13] = 0;
  for (uint8_t i = 0; i < 13; i++){
    arr[13] += arr[i];
  }
  return;  
}