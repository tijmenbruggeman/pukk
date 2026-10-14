idf_component_register(
    SRCS "battle.c" "battle_rules.c" "battle_msg.c"
    INCLUDE_DIRS "include"
    REQUIRES packs ble
    PRIV_REQUIRES esp_timer
)
