#ifndef AMBYTE_BME280_ADAFRUIT_PRIVATE_HPP
#define AMBYTE_BME280_ADAFRUIT_PRIVATE_HPP

#include "bme280.h"

#include "Arduino.h"
#include "I2C_device.h"

#if defined(BME280_ENABLE_ADAFRUIT_SENSOR_API)
#include <Adafruit_Sensor.h>
#endif

#define BME280_ADDRESS (BME280_I2C_ADDR_PRIMARY)
#define BME280_ADDRESS_ALTERNATE (BME280_I2C_ADDR_SECONDARY)

enum {
  BME280_REGISTER_DIG_T1 = 0x88,
  BME280_REGISTER_DIG_T2 = 0x8A,
  BME280_REGISTER_DIG_T3 = 0x8C,

  BME280_REGISTER_DIG_P1 = 0x8E,
  BME280_REGISTER_DIG_P2 = 0x90,
  BME280_REGISTER_DIG_P3 = 0x92,
  BME280_REGISTER_DIG_P4 = 0x94,
  BME280_REGISTER_DIG_P5 = 0x96,
  BME280_REGISTER_DIG_P6 = 0x98,
  BME280_REGISTER_DIG_P7 = 0x9A,
  BME280_REGISTER_DIG_P8 = 0x9C,
  BME280_REGISTER_DIG_P9 = 0x9E,

  BME280_REGISTER_DIG_H1 = 0xA1,
  BME280_REGISTER_DIG_H2 = 0xE1,
  BME280_REGISTER_DIG_H3 = 0xE3,
  BME280_REGISTER_DIG_H4 = 0xE4,
  BME280_REGISTER_DIG_H5 = 0xE5,
  BME280_REGISTER_DIG_H6 = 0xE7,

  BME280_REGISTER_CHIPID = 0xD0,
  BME280_REGISTER_VERSION = 0xD1,
  BME280_REGISTER_SOFTRESET = 0xE0,

  BME280_REGISTER_CAL26 = 0xE1,

  BME280_REGISTER_CONTROLHUMID = 0xF2,
  BME280_REGISTER_STATUS = 0xF3,
  BME280_REGISTER_CONTROL = 0xF4,
  BME280_REGISTER_CONFIG = 0xF5,
  BME280_REGISTER_PRESSUREDATA = 0xF7,
  BME280_REGISTER_TEMPDATA = 0xFA,
  BME280_REGISTER_HUMIDDATA = 0xFD
};

typedef struct {
  uint16_t dig_T1;
  int16_t dig_T2;
  int16_t dig_T3;

  uint16_t dig_P1;
  int16_t dig_P2;
  int16_t dig_P3;
  int16_t dig_P4;
  int16_t dig_P5;
  int16_t dig_P6;
  int16_t dig_P7;
  int16_t dig_P8;
  int16_t dig_P9;

  uint8_t dig_H1;
  int16_t dig_H2;
  uint8_t dig_H3;
  int16_t dig_H4;
  int16_t dig_H5;
  int8_t dig_H6;
} bme280_calib_data;

class Adafruit_BME280;

#if defined(BME280_ENABLE_ADAFRUIT_SENSOR_API)
class Adafruit_BME280_Temp : public Adafruit_Sensor {
public:
  Adafruit_BME280_Temp(Adafruit_BME280 *parent) { _theBME280 = parent; }
  bool getEvent(sensors_event_t *);
  void getSensor(sensor_t *);

private:
  int _sensorID = 280;
  Adafruit_BME280 *_theBME280 = NULL;
};

class Adafruit_BME280_Pressure : public Adafruit_Sensor {
public:
  Adafruit_BME280_Pressure(Adafruit_BME280 *parent) { _theBME280 = parent; }
  bool getEvent(sensors_event_t *);
  void getSensor(sensor_t *);

private:
  int _sensorID = 280;
  Adafruit_BME280 *_theBME280 = NULL;
};

class Adafruit_BME280_Humidity : public Adafruit_Sensor {
public:
  Adafruit_BME280_Humidity(Adafruit_BME280 *parent) { _theBME280 = parent; }
  bool getEvent(sensors_event_t *);
  void getSensor(sensor_t *);

private:
  int _sensorID = 280;
  Adafruit_BME280 *_theBME280 = NULL;
};
#endif

class Adafruit_BME280 {
public:
  enum sensor_sampling {
    SAMPLING_NONE = 0b000,
    SAMPLING_X1 = 0b001,
    SAMPLING_X2 = 0b010,
    SAMPLING_X4 = 0b011,
    SAMPLING_X8 = 0b100,
    SAMPLING_X16 = 0b101
  };

  enum sensor_mode {
    MODE_SLEEP = 0b00,
    MODE_FORCED = 0b01,
    MODE_NORMAL = 0b11
  };

  enum sensor_filter {
    FILTER_OFF = 0b000,
    FILTER_X2 = 0b001,
    FILTER_X4 = 0b010,
    FILTER_X8 = 0b011,
    FILTER_X16 = 0b100
  };

  enum standby_duration {
    STANDBY_MS_0_5 = 0b000,
    STANDBY_MS_10 = 0b110,
    STANDBY_MS_20 = 0b111,
    STANDBY_MS_62_5 = 0b001,
    STANDBY_MS_125 = 0b010,
    STANDBY_MS_250 = 0b011,
    STANDBY_MS_500 = 0b100,
    STANDBY_MS_1000 = 0b101
  };

  Adafruit_BME280();
  ~Adafruit_BME280(void);
  bool begin(uint8_t addr = BME280_ADDRESS, TwoWire *theWire = &Wire);
  bool init();

  void setSampling(sensor_mode mode = MODE_NORMAL,
                   sensor_sampling tempSampling = SAMPLING_X16,
                   sensor_sampling pressSampling = SAMPLING_X16,
                   sensor_sampling humSampling = SAMPLING_X16,
                   sensor_filter filter = FILTER_OFF,
                   standby_duration duration = STANDBY_MS_0_5);

  bool takeForcedMeasurement(void);
  float readTemperature(void);
  float readPressure(void);
  float readHumidity(void);

  float readAltitude(float seaLevel);
  float seaLevelForAltitude(float altitude, float pressure);
  uint32_t sensorID(void);

  float getTemperatureCompensation(void);
  void setTemperatureCompensation(float);

#if defined(BME280_ENABLE_ADAFRUIT_SENSOR_API)
  Adafruit_Sensor *getTemperatureSensor(void);
  Adafruit_Sensor *getPressureSensor(void);
  Adafruit_Sensor *getHumiditySensor(void);
#endif

protected:
  I2C_device *i2c_dev = nullptr;

#if defined(BME280_ENABLE_ADAFRUIT_SENSOR_API)
  Adafruit_BME280_Temp *temp_sensor = NULL;
  Adafruit_BME280_Pressure *pressure_sensor = NULL;
  Adafruit_BME280_Humidity *humidity_sensor = NULL;
#endif

  void readCoefficients(void);
  bool isReadingCalibration(void);

  void write8(byte reg, byte value);
  uint8_t read8(byte reg);
  uint16_t read16(byte reg);
  uint32_t read24(byte reg);
  int16_t readS16(byte reg);
  uint16_t read16_LE(byte reg);
  int16_t readS16_LE(byte reg);

  uint8_t _i2caddr;
  int32_t _sensorID;
  int32_t t_fine;
  int32_t t_fine_adjust = 0;
  bme280_calib_data _bme280_calib;

  struct config {
    unsigned int t_sb : 3;
    unsigned int filter : 3;
    unsigned int none : 1;
    unsigned int spi3w_en : 1;

    unsigned int get() { return (t_sb << 5) | (filter << 2) | spi3w_en; }
  };
  config _configReg;

  struct ctrl_meas {
    unsigned int osrs_t : 3;
    unsigned int osrs_p : 3;
    unsigned int mode : 2;

    unsigned int get() { return (osrs_t << 5) | (osrs_p << 2) | mode; }
  };
  ctrl_meas _measReg;

  struct ctrl_hum {
    unsigned int none : 5;
    unsigned int osrs_h : 3;

    unsigned int get() { return (osrs_h); }
  };
  ctrl_hum _humReg;
};

#endif
