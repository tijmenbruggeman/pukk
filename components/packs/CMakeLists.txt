idf_component_register(
    SRCS "pack_format.c" "packs.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES storage vfs esp_timer
)
