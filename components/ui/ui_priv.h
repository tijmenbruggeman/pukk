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
#include "battle.h"
#include "pack_format.h"
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
/* Rounded box with one small label; for scores and progress. */
lv_obj_t *ui_pill(lv_obj_t *parent, lv_obj_t **label, lv_color_t color);
/* First character of a title (a whole UTF-8 sequence), upper-cased if ASCII. */
void ui_initial(const char *title, char *out, size_t size);
/* Pill-shaped button, UI_BUTTON_H high. */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, int32_t width,
                    lv_event_cb_t cb, void *user_data);

/* View switching (ui.c). Not from inside a click event of the current view:
 * that deletes the object being clicked; use lv_async_call(). */
void ui_go_home(void);
void ui_go_pack(const char *pack_name);
void ui_go_game(const char *pack_name);

/* Splash (screen_splash.c). */
void splash_show(void);

/* Pack list (screen_home.c). */
void home_show(void);
void home_forget(void);
void home_refresh(void);
void home_update_status(void);

/* Pack page: play alone or start a battle (screen_pack.c). Returns false if
 * the pack cannot be read. */
bool pack_show(const char *pack_name);

/* Solo game and result (screen_game.c). Returns false if the pack cannot be played. */
bool game_start(const char *pack_name);
void game_stop(void);

/* Question screen shared by game and battle (quiz_view.c). */
typedef void (*quiz_answer_cb_t)(int choice);
typedef void (*quiz_timeout_cb_t)(void);
/* Timer, question and tiles on `view`; the header pills are the caller's. */
void quiz_build(lv_obj_t *view, quiz_answer_cb_t on_answer, quiz_timeout_cb_t on_timeout);
/* Show a question and start its timer. Taps go to on_answer until locked. */
void quiz_show(const pack_question_t *q);
/* Stop the timer and ignore taps. */
void quiz_lock(void);
/* Battle: `choice` is picked, the others dim; the timer runs on. */
void quiz_mark_choice(int choice);
/* Mark the right answer and, if different, the wrong one picked (or -1). */
void quiz_reveal(int correct, int chosen);
/* Battle: show which answer the opponent picked. */
void quiz_mark_opponent(int choice);
/* Replace the timer with a message. */
void quiz_feedback(const char *text, lv_color_t bg, lv_color_t fg);
void quiz_stop(void);

/* Battle views (screen_battle.c), driven by the battle engine's events. */
void battle_view_event(const battle_event_t *event);
void battle_view_stop(void);

/* Pop-up on the top layer (upload_overlay.c); stays across view switches. */
void overlay_upload_progress(const char *name, uint32_t received, uint32_t total);
void overlay_upload_finished(const char *name, int result);
void overlay_message(const char *text, lv_color_t color);
