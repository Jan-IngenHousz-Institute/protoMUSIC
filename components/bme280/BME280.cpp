/*!
 * @file BME280.cpp
 *
 * @mainpage Adafruit BME280 humidity, temperature & pressure sensor
 *
 * @section intro_sec Introduction
 *
 *  Driver for the BME280 humidity, temperature & pressure sensor
 *
 * These sensors use I2C or SPI to communicate, 2 or 4 pins are required
 * to interface.
 *
 * Designed specifically to work with the Adafruit BME280 Breakout
 * ----> http://www.adafruit.com/products/2652
 *
 *  Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing
 *  products from Adafruit!
 *
 * @section author Author
 *
 * Written by Kevin "KTOWN" Townsend for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text here must be included in any redistribution.
 * See the LICENSE file for details.
 *
 */

#include "bme280_adafruit_private.hpp"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BME280_I2C_TIMEOUT_MS 100

/*!
 *  @brief  class constructor
 */
Adafruit_BME280::Adafruit_BME280() {}

Adafruit_BME280::~Adafruit_BME280(void) {}

/*!
 *   @brief  Initialise sensor with given parameters / settings
 *   @param port the I2C port number
 *   @param addr the I2C address the device can be found on
 *   @returns true on success, false otherwise
 */
bool Adafruit_BME280::begin(i2c_port_t port, uint8_t addr) {
  port_ = port;
  addr_ = addr;

  if (i2c_bus_probe_addr(port_, addr_) != ESP_OK) {
    return false;
  }
  return init();
}

/*!
 *   @brief  Initialise sensor with given parameters / settings
 *   @returns true on success, false otherwise
 */
bool Adafruit_BME280::init() {
  // check if sensor, i.e. the chip ID is correct
  _sensorID = read8(BME280_REGISTER_CHIPID);
  if (_sensorID != 0x60)
    return false;

  // reset the device using soft-reset
  // this makes sure the IIR is off, etc.
  write8(BME280_REGISTER_SOFTRESET, 0xB6);

  // wait for chip to wake up.
  vTaskDelay(pdMS_TO_TICKS(10));

  // if chip is still reading calibration, delay
  while (isReadingCalibration())
    vTaskDelay(pdMS_TO_TICKS(10));

  readCoefficients(); // read trimming parameters, see DS 4.2.2

  setSampling(); // use defaults

  vTaskDelay(pdMS_TO_TICKS(100));

  return true;
}

/*!
 *   @brief  setup sensor with given parameters / settings
 *
 *   @param mode the power mode to use for the sensor
 *   @param tempSampling the temp samping rate to use
 *   @param pressSampling the pressure sampling rate to use
 *   @param humSampling the humidity sampling rate to use
 *   @param filter the filter mode to use
 *   @param duration the standby duration to use
 */
void Adafruit_BME280::setSampling(sensor_mode mode,
                                  sensor_sampling tempSampling,
                                  sensor_sampling pressSampling,
                                  sensor_sampling humSampling,
                                  sensor_filter filter,
                                  standby_duration duration) {
  _measReg.mode = mode;
  _measReg.osrs_t = tempSampling;
  _measReg.osrs_p = pressSampling;

  _humReg.osrs_h = humSampling;
  _configReg.filter = filter;
  _configReg.t_sb = duration;
  _configReg.spi3w_en = 0;

  // making sure sensor is in sleep mode before setting configuration
  // as it otherwise may be ignored
  write8(BME280_REGISTER_CONTROL, MODE_SLEEP);

  // you must make sure to also set REGISTER_CONTROL after setting the
  // CONTROLHUMID register, otherwise the values won't be applied (see
  // DS 5.4.3)
  write8(BME280_REGISTER_CONTROLHUMID, _humReg.get());
  write8(BME280_REGISTER_CONFIG, _configReg.get());
  write8(BME280_REGISTER_CONTROL, _measReg.get());
}

/*!
 *   @brief  Writes an 8 bit value over I2C or SPI
 *   @param reg the register address to write to
 *   @param value the value to write to the register
 */
void Adafruit_BME280::write8(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = { reg, value };
  if (i2c_bus_lock(pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS)) != ESP_OK) {
    return;
  }
  i2c_master_write_to_device(port_, addr_, buf, sizeof(buf),
                             pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS));
  i2c_bus_unlock();
}

/*!
 *   @brief  Reads an 8 bit value over I2C
 *   @param reg the register address to read from
 *   @returns the data byte read from the device
 */
uint8_t Adafruit_BME280::read8(uint8_t reg) {
  uint8_t val = 0;
  if (i2c_bus_lock(pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS)) != ESP_OK) {
    return 0;
  }
  i2c_master_write_read_device(port_, addr_, &reg, 1, &val, 1,
                               pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS));
  i2c_bus_unlock();
  return val;
}

/*!
 *   @brief  Reads a 16 bit value over I2C
 *   @param reg the register address to read from
 *   @returns the 16 bit data value read from the device
 */
uint16_t Adafruit_BME280::read16(uint8_t reg) {
  uint8_t buffer[2] = {0};
  if (i2c_bus_lock(pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS)) != ESP_OK) {
    return 0;
  }
  i2c_master_write_read_device(port_, addr_, &reg, 1, buffer, sizeof(buffer),
                               pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS));
  i2c_bus_unlock();
  return uint16_t(buffer[0]) << 8 | uint16_t(buffer[1]);
}

/*!
 *   @brief  Reads a signed 16 bit little endian value over I2C or SPI
 *   @param reg the register address to read from
 *   @returns the 16 bit data value read from the device
 */
uint16_t Adafruit_BME280::read16_LE(uint8_t reg) {
  uint16_t temp = read16(reg);
  return (temp >> 8) | (temp << 8);
}

/*!
 *   @brief  Reads a signed 16 bit value over I2C or SPI
 *   @param reg the register address to read from
 *   @returns the 16 bit data value read from the device
 */
int16_t Adafruit_BME280::readS16(uint8_t reg) { return (int16_t)read16(reg); }

/*!
 *   @brief  Reads a signed little endian 16 bit value over I2C or SPI
 *   @param reg the register address to read from
 *   @returns the 16 bit data value read from the device
 */
int16_t Adafruit_BME280::readS16_LE(uint8_t reg) {
  return (int16_t)read16_LE(reg);
}

/*!
 *   @brief  Reads a 24 bit value over I2C
 *   @param reg the register address to read from
 *   @returns the 24 bit data value read from the device
 */
uint32_t Adafruit_BME280::read24(uint8_t reg) {
  uint8_t buffer[3] = {0};
  if (i2c_bus_lock(pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS)) != ESP_OK) {
    return 0;
  }
  i2c_master_write_read_device(port_, addr_, &reg, 1, buffer, sizeof(buffer),
                               pdMS_TO_TICKS(BME280_I2C_TIMEOUT_MS));
  i2c_bus_unlock();

  return uint32_t(buffer[0]) << 16 | uint32_t(buffer[1]) << 8 |
         uint32_t(buffer[2]);
}

/*!
 *  @brief  Take a new measurement (only possible in forced mode)
    @returns true in case of success else false
 */
bool Adafruit_BME280::takeForcedMeasurement(void) {
  bool return_value = false;
  // If we are in forced mode, the BME sensor goes back to sleep after each
  // measurement and we need to set it to forced mode once at this point, so
  // it will take the next measurement and then return to sleep again.
  // In normal mode simply does new measurements periodically.
  if (_measReg.mode == MODE_FORCED) {
    return_value = true;
    // set to forced mode, i.e. "take next measurement"
    write8(BME280_REGISTER_CONTROL, _measReg.get());
    // Store current time to measure the timeout
    uint32_t timeout_start = (uint32_t)(esp_timer_get_time() / 1000);
    // wait until measurement has been completed, otherwise we would read the
    // the values from the last measurement or the timeout occurred after 2 sec.
    while (read8(BME280_REGISTER_STATUS) & 0x08) {
      // In case of a timeout, stop the while loop
      if (((uint32_t)(esp_timer_get_time() / 1000) - timeout_start) > 2000) {
        return_value = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  return return_value;
}

/*!
 *   @brief  Reads the factory-set coefficients
 */
void Adafruit_BME280::readCoefficients(void) {
  _bme280_calib.dig_T1 = read16_LE(BME280_REGISTER_DIG_T1);
  _bme280_calib.dig_T2 = readS16_LE(BME280_REGISTER_DIG_T2);
  _bme280_calib.dig_T3 = readS16_LE(BME280_REGISTER_DIG_T3);

  _bme280_calib.dig_P1 = read16_LE(BME280_REGISTER_DIG_P1);
  _bme280_calib.dig_P2 = readS16_LE(BME280_REGISTER_DIG_P2);
  _bme280_calib.dig_P3 = readS16_LE(BME280_REGISTER_DIG_P3);
  _bme280_calib.dig_P4 = readS16_LE(BME280_REGISTER_DIG_P4);
  _bme280_calib.dig_P5 = readS16_LE(BME280_REGISTER_DIG_P5);
  _bme280_calib.dig_P6 = readS16_LE(BME280_REGISTER_DIG_P6);
  _bme280_calib.dig_P7 = readS16_LE(BME280_REGISTER_DIG_P7);
  _bme280_calib.dig_P8 = readS16_LE(BME280_REGISTER_DIG_P8);
  _bme280_calib.dig_P9 = readS16_LE(BME280_REGISTER_DIG_P9);

  _bme280_calib.dig_H1 = read8(BME280_REGISTER_DIG_H1);
  _bme280_calib.dig_H2 = readS16_LE(BME280_REGISTER_DIG_H2);
  _bme280_calib.dig_H3 = read8(BME280_REGISTER_DIG_H3);
  _bme280_calib.dig_H4 = ((int8_t)read8(BME280_REGISTER_DIG_H4) << 4) |
                         (read8(BME280_REGISTER_DIG_H4 + 1) & 0xF);
  _bme280_calib.dig_H5 = ((int8_t)read8(BME280_REGISTER_DIG_H5 + 1) << 4) |
                         (read8(BME280_REGISTER_DIG_H5) >> 4);
  _bme280_calib.dig_H6 = (int8_t)read8(BME280_REGISTER_DIG_H6);
}

/*!
 *   @brief return true if chip is busy reading cal data
 *   @returns true if reading calibration, false otherwise
 */
bool Adafruit_BME280::isReadingCalibration(void) {
  uint8_t const rStatus = read8(BME280_REGISTER_STATUS);

  return (rStatus & (1 << 0)) != 0;
}

/*!
 *   @brief  Returns the temperature from the sensor
 *   @returns the temperature read from the device or NaN if sampling off
 */
float Adafruit_BME280::readTemperature(void) {
  int32_t var1, var2;
  if (_measReg.osrs_t == sensor_sampling::SAMPLING_NONE)
    return NAN;

  int32_t adc_T = read24(BME280_REGISTER_TEMPDATA);
  adc_T >>= 4;

  var1 = (int32_t)((adc_T / 8) - ((int32_t)_bme280_calib.dig_T1 * 2));
  var1 = (var1 * ((int32_t)_bme280_calib.dig_T2)) / 2048;
  var2 = (int32_t)((adc_T / 16) - ((int32_t)_bme280_calib.dig_T1));
  var2 = (((var2 * var2) / 4096) * ((int32_t)_bme280_calib.dig_T3)) / 16384;

  t_fine = var1 + var2 + t_fine_adjust;

  int32_t T = (t_fine * 5 + 128) / 256;

  return (float)T / 100;
}

/*!
 *   @brief  Returns the pressure from the sensor
 *   @returns the pressure value (in Pascal) or NaN if sampling off
 */
float Adafruit_BME280::readPressure(void) {
  int64_t var1, var2, var3, var4;
  if (_measReg.osrs_p == sensor_sampling::SAMPLING_NONE)
    return NAN;

  readTemperature(); // must be done first to get t_fine

  int32_t adc_P = read24(BME280_REGISTER_PRESSUREDATA);
  adc_P >>= 4;

  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)_bme280_calib.dig_P6;
  var2 = var2 + ((var1 * (int64_t)_bme280_calib.dig_P5) * 131072);
  var2 = var2 + (((int64_t)_bme280_calib.dig_P4) * 34359738368);
  var1 = ((var1 * var1 * (int64_t)_bme280_calib.dig_P3) / 256) +
         ((var1 * ((int64_t)_bme280_calib.dig_P2) * 4096));
  var3 = ((int64_t)1) * 140737488355328;
  var1 = (var3 + var1) * ((int64_t)_bme280_calib.dig_P1) / 8589934592;

  if (var1 == 0) {
    return 0; // avoid exception caused by division by zero
  }

  var4 = 1048576 - adc_P;
  var4 = (((var4 * 2147483648) - var2) * 3125) / var1;
  var1 = (((int64_t)_bme280_calib.dig_P9) * (var4 / 8192) * (var4 / 8192)) /
         33554432;
  var2 = (((int64_t)_bme280_calib.dig_P8) * var4) / 524288;
  var4 = ((var4 + var1 + var2) / 256) + (((int64_t)_bme280_calib.dig_P7) * 16);

  float P = var4 / 256.0;

  return P;
}

/*!
 *  @brief  Returns the humidity from the sensor
 *  @returns the humidity value read from the device or NaN if sampling off
 */
float Adafruit_BME280::readHumidity(void) {
  int32_t var1, var2, var3, var4, var5;
  if (_humReg.osrs_h == sensor_sampling::SAMPLING_NONE)
    return NAN;

  readTemperature(); // must be done first to get t_fine

  int32_t adc_H = read16(BME280_REGISTER_HUMIDDATA);
  var1 = t_fine - ((int32_t)76800);
  var2 = (int32_t)(adc_H * 16384);
  var3 = (int32_t)(((int32_t)_bme280_calib.dig_H4) * 1048576);
  var4 = ((int32_t)_bme280_calib.dig_H5) * var1;
  var5 = (((var2 - var3) - var4) + (int32_t)16384) / 32768;
  var2 = (var1 * ((int32_t)_bme280_calib.dig_H6)) / 1024;
  var3 = (var1 * ((int32_t)_bme280_calib.dig_H3)) / 2048;
  var4 = ((var2 * (var3 + (int32_t)32768)) / 1024) + (int32_t)2097152;
  var2 = ((var4 * ((int32_t)_bme280_calib.dig_H2)) + 8192) / 16384;
  var3 = var5 * var2;
  var4 = ((var3 / 32768) * (var3 / 32768)) / 128;
  var5 = var3 - ((var4 * ((int32_t)_bme280_calib.dig_H1)) / 16);
  var5 = (var5 < 0 ? 0 : var5);
  var5 = (var5 > 419430400 ? 419430400 : var5);
  uint32_t H = (uint32_t)(var5 / 4096);

  return (float)H / 1024.0;
}

/*!
 *   Calculates the altitude (in meters) from the specified atmospheric
 *   pressure (in hPa), and sea-level pressure (in hPa).
 *   @param  seaLevel      Sea-level pressure in hPa
 *   @returns the altitude value read from the device
 */
float Adafruit_BME280::readAltitude(float seaLevel) {
  // Equation taken from BMP180 datasheet (page 16):
  //  http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf

  // Note that using the equation from wikipedia can give bad results
  // at high altitude. See this thread for more information:
  //  http://forums.adafruit.com/viewtopic.php?f=22&t=58064

  float atmospheric = readPressure() / 100.0F;
  return 44330.0 * (1.0 - pow(atmospheric / seaLevel, 0.1903));
}

/*!
 *   Calculates the pressure at sea level (in hPa) from the specified
 * altitude (in meters), and atmospheric pressure (in hPa).
 *   @param  altitude      Altitude in meters
 *   @param  atmospheric   Atmospheric pressure in hPa
 *   @returns the pressure at sea level (in hPa) from the specified altitude
 */
float Adafruit_BME280::seaLevelForAltitude(float altitude, float atmospheric) {
  // Equation taken from BMP180 datasheet (page 17):
  //  http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf

  // Note that using the equation from wikipedia can give bad results
  // at high altitude. See this thread for more information:
  //  http://forums.adafruit.com/viewtopic.php?f=22&t=58064

  return atmospheric / pow(1.0 - (altitude / 44330.0), 5.255);
}

/*!
 *   Returns Sensor ID found by init() for diagnostics
 *   @returns Sensor ID 0x60 for BME280, 0x56, 0x57, 0x58 BMP280
 */
uint32_t Adafruit_BME280::sensorID(void) { return _sensorID; }

/*!
 *   Returns the current temperature compensation value in degrees Celsius
 *   @returns the current temperature compensation value in degrees Celsius
 */
float Adafruit_BME280::getTemperatureCompensation(void) {
  return float((t_fine_adjust * 5) >> 8) / 100.0;
};

/*!
 *  Sets a value to be added to each temperature reading. This adjusted
 *  temperature is used in pressure and humidity readings.
 *  @param  adjustment  Value to be added to each temperature reading in Celsius
 */
void Adafruit_BME280::setTemperatureCompensation(float adjustment) {
  // convert the value in C into and adjustment to t_fine
  t_fine_adjust = ((int32_t(adjustment * 100) << 8)) / 5;
};
