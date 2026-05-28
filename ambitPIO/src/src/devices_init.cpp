#include "devices_init.h"


static const char* TAG = "Device_init";
bool SPI_bus_initialized = false;
bool I2C_bus_initialized = false;

spi_bus_config_t SPI_bus_cfg = {
		.mosi_io_num=ADPD_SPI_MOSI,
    .miso_io_num=ADPD_SPI_MISO,
		.sclk_io_num=ADPD_SPI_SCK, 
    .quadwp_io_num=-1,
    .quadhd_io_num=-1,
    .max_transfer_sz = 4092,
	};

bool init_spi_bus(void){
  esp_err_t err = 1;

  ESP_LOGV(TAG, "Initiate SPI BUS");
  if (!SPI_bus_initialized){
      err = spi_bus_initialize(SPI2_HOST, &SPI_bus_cfg, SPI_DMA_CH_AUTO);
      if (err == ESP_OK) {
          SPI_bus_initialized = true;
          ESP_LOGI(TAG, "SPI BUS initiated");
          return true;
      }
      else{
          ESP_LOGE(TAG, "SPI BUS init FAILED: %s", esp_err_to_name(err));
          SPI_bus_initialized = false;
          return false;
      }
  }else{
    ESP_LOGI(TAG, "SPI bus previously initialized");
    return true;
  }
}

bool init_i2c_bus(void){
  bool err = false;
  ESP_LOGV(TAG, "Initiate I2C BUS");
  if (!I2C_bus_initialized){
      err = Wire.begin(I2C_SDA, I2C_SCL, 400000UL);
      if (err == true) {
          I2C_bus_initialized = true;
          ESP_LOGI(TAG, "I2C BUS initiated");
          return true;
      }
      else{
          ESP_LOGE(TAG, "I2C BUS Failed");
          I2C_bus_initialized = false;
          return false;
      }
  }else{
    ESP_LOGI(TAG, "I2C bus previously initialized");
    return true;
  }
}

void i2c_scan(){
  if (!I2C_bus_initialized) init_i2c_bus();
  if (!I2C_bus_initialized) return;

  for (uint8_t i = 1; i < 127; i++){
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0){

      switch (i){
        case ADDR_I2C_AS7341:
        {
          Serial.print("AS7341 found at ");
        }
        break;

      

        case ADDR_I2C_MLX90632:
        {
          Serial.print("MLX90632 found at ");
        }
        break;

        default:
        {
          Serial.print("Something else found at ");
        }

      }
      Serial.println(i);
    }
  }
}

// bool i2c_scan(uint8_t i){
//   if (!I2C_bus_initialized) init_i2c_bus();
//   if (!I2C_bus_initialized) return false;

//   Wire.beginTransmission(i);
//   if (Wire.endTransmission() == 0){
//     Serial.println("Found");
//     return true;
//   }
//   return false;
// }