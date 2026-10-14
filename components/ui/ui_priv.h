/*
 * Internal UI interface. Everything here must be called with the UI lock held
 * (LVGL callbacks already hold it).
 *
 * All views are built on the one active screen object: switching views cleans
 * it, so a module must not touch its objects once another view is shown.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#define UI_COLOR_ACCENT     lv_color_hex(0xFFC233)
#define UI_COLOR_TEXT       lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM   lv_color_hex(0x8A8A90)
#define UI_COLOR_BUTTON     lv_color_hex(0x26262B)
#define UI_COLOR_BUTTON_DIM lv_color_hex(0x151517)
#define UI_COLOR_PANEL      lv_color_hex(0x1E1E22)
#define UI_COLOR_GOOD       lv_color_hex(0x22C55E)
#define UI_COLOR_WARN       lv_color_hex(0xF59E0B)
#define UI_COLOR_BAD        lv_color_hex(0xEF4444)

#define UI_FONT_SMALL       (&lv_font_montserrat_14)
#define UI_FONT_NORMAL      (&lv_font_montserrat_24)
#define UI_FONT_LARGE       (&lv_font_montserrat_40)

/* Clean the active screen and give it the black background. */
lv_obj_t *ui_clean_screen(void);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
const char *ui_device_name(void);

/* View switching (ui.c). */
void ui_go_home(void);
void ui_go_game(const char *pack_name);

/* Pack list (screen_home.c). */
void home_show(void);
void home_forget(void);
void home_refresh(void);
void home_update_device_name(void);

/* Game (screen_game.c). Returns false if the pack cannot be played. */
bool game_start(const char *pack_name);
void game_stop(void);

/* Pop-up on the top layer (upload_overlay.c); stays across view switches. */
void overlay_upload_progress(const char *name, uint32_t received, uint32_t total);
void overlay_upload_finished(const char *name, int result);
void overlay_message(const char *text, lv_color_t color);
