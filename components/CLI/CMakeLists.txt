idf_component_register(
    SRCS "CLI.c"
    INCLUDE_DIRS "include"
    REQUIRES device_commands i2c_bus time_sync uart_sensors
    PRIV_REQUIRES console driver wifi_manager
)
