idf_component_register(
    SRCS
        "BME280.cpp"
        "bme280_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain
    PRIV_REQUIRES i2c_bus esp_timer
)
