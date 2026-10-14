/* LVGL config for the host preview. Mirrors the firmware's sdkconfig where
 * it affects rendering (16-bit color, fonts, dark theme). */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (64 * 1024U)
#define LV_USE_OS               LV_OS_NONE
#define LV_DEF_REFR_PERIOD      33

#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_40   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

#define LV_USE_SYSMON           0
#define LV_BUILD_EXAMPLES       0
#define LV_BUILD_DEMOS          0

#endif
