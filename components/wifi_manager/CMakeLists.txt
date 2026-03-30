idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES protocomm
    PRIV_REQUIRES esp_event esp_netif esp_wifi wifi_provisioning bt nvs_flash
)
