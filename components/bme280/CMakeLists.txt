idf_component_register(
    SRCS
        "BME280.cpp"
        "bme280_arduino_shim.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES framework-arduinoespressif32 I2C_device i2c_bus
)
