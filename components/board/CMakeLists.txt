idf_component_register(
    SRCS "board_i2c.c" "pmu_axp2101.c" "display_co5300.c" "touch_cst9217.c" "buttons.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_lcd
)
