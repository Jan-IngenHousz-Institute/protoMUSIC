idf_component_register(
    SRCS "sqlite_persistence.c"
    INCLUDE_DIRS "include"
    REQUIRES domain sqlite3 sd_card
    PRIV_REQUIRES littlefs nvs_flash
)
