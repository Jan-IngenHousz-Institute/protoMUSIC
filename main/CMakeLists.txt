idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_bus wifi_manager nvs_flash ambyte_status lua_runner pcf2131tfy_rtc bme280 CLI device_commands persistence sd_card sd_logger domain littlefs mqtt_client device_config certs uart_sensors sync_runner esp_event esp_netif esp_wifi
)
