idf_component_register(
    SRCS "sd_card.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver fatfs sdmmc
)
