idf_component_register(
    SRCS "I2C_device.cpp"
    INCLUDE_DIRS "include"
    REQUIRES framework-arduinoespressif32 i2c_bus
)
