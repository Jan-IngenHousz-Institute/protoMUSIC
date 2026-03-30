idf_component_register(
    SRCS "device_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES domain
    PRIV_REQUIRES json
)
