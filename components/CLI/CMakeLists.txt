idf_component_register(
    SRCS "CLI.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c_bus
    PRIV_REQUIRES bme280 pcf2131tfy_rtc ambyte_status console
)
