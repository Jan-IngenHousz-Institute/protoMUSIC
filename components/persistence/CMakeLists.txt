idf_component_register(
    SRCS "pending_store.c" "sqlite_persistence.c"
    INCLUDE_DIRS "include"
    REQUIRES domain sqlite3 sd_card
    PRIV_REQUIRES littlefs nvs_flash
)
