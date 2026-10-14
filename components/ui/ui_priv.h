/*
 * Internal UI interface. Everything here must be called with the UI lock held
 * (LVGL callbacks already hold it).
 *
 * Each view lives in one container on the active screen: switching views
 * cleans the screen, so a module must not touch its objects once another view
 * is shown.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_theme.h"

/* Device status shown in the status bar (ui.c). */
typedef struct {
    char device_name[24];
    int battery_percent;    /* -1: no battery (USB only) */
    bool charging;
    bool ble_connected;
} ui_status_t;

const ui_status_t *ui_status(void);

/* Clean the active screen for a new view. */
void ui_clean_screen(void);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* View switching (ui.c). */
void ui_go_home(void);
void ui_go_game(const char *pack_name);

/* Splash (screen_splash.c). */
void splash_show(void);

/* Pack list (screen_home.c). */
void home_show(void);
void home_forget(void);
void home_refresh(void);
void home_update_status(void);

/* Game and result (screen_game.c). Returns false if the pack cannot be played. */
bool game_start(const char *pack_name);
void game_stop(void);

/* Pop-up on the top layer (upload_overlay.c); stays across view switches. */
void overlay_upload_progress(const char *name, uint32_t received, uint32_t total);
void overlay_upload_finished(const char *name, int result);
void overlay_message(const char *text, lv_color_t color);
