idf_component_register(
    SRCS "lvgl_port.c" "quiz_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES board
    PRIV_REQUIRES esp_timer
)

# Make LVGL's performance monitor report whole-system CPU load instead of
# only the LVGL task's own load (implemented in lvgl_port.c).
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE LV_SYSMON_GET_IDLE=ui_cpu_idle_percent)
