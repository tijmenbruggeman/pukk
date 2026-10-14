idf_component_register(
    SRCS "pukk_ble.c" "protocol.c" "ble_central.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt esp_app_format storage packs
)
