/*
 * Pukk house style ("speels"): Fredoka, black AMOLED background, a yellow
 * brand color and four answer colors, each with its own shape.
 *
 * Call with the UI lock held.
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"
#include "assets/pukk_assets.h"

/* Palette */
#define UI_COLOR_BG             lv_color_hex(0x000000)
#define UI_COLOR_SURFACE        lv_color_hex(0x1C1C24)
#define UI_COLOR_SURFACE_2      lv_color_hex(0x2A2A35)
#define UI_COLOR_TEXT           lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM       lv_color_hex(0x8C8C9A)
#define UI_COLOR_BRAND          lv_color_hex(0xFFD23F)
#define UI_COLOR_ON_BRAND       lv_color_hex(0x2E2300)
#define UI_COLOR_GOOD           lv_color_hex(0x2BC48A)
#define UI_COLOR_BAD            lv_color_hex(0xFF5A5F)
#define UI_COLOR_WARN           lv_color_hex(0xFFB020)
#define UI_COLOR_BLE            lv_color_hex(0x3D8BFF)
#define UI_COLOR_ON_GOOD        lv_color_hex(0x06301F)
#define UI_COLOR_ON_WARN        lv_color_hex(0x3A2500)
/* The other Pukk in a battle; we are UI_COLOR_BRAND. */
#define UI_COLOR_OPPONENT       lv_color_hex(0x3D8BFF)

/* Typography */
#define UI_FONT_SMALL           (&pukk_font_20)   /* labels, status; has symbols */
#define UI_FONT_BODY            (&pukk_font_24)   /* answers, list titles */
#define UI_FONT_QUESTION        (&pukk_font_28)
#define UI_FONT_TITLE           (&pukk_font_32)   /* has symbols */
#define UI_FONT_DISPLAY         (&pukk_font_48)
#define UI_FONT_LOGO            (&pukk_font_logo) /* only "pukk" */

/* Symbols present in UI_FONT_SMALL and UI_FONT_TITLE (FontAwesome via LVGL). */
#define UI_SYMBOL_STAR          "\xEF\x80\x85"
#define UI_SYMBOL_OK            LV_SYMBOL_OK
#define UI_SYMBOL_CLOSE         LV_SYMBOL_CLOSE

#define UI_RADIUS_CARD          20
#define UI_SCREEN_PAD           16
#define UI_BUTTON_H             64

typedef struct {
    lv_color_t bg;
    lv_color_t fg;          /* text and shape on bg */
    const lv_image_dsc_t *shape;
} ui_answer_style_t;

void ui_theme_init(void);

/* Answer i (0..3): color pair and shape, the same for every question. */
const ui_answer_style_t *ui_answer_style(int index);
/* A stable accent color for a pack, picked from its name. */
lv_color_t ui_pack_color(const char *name, lv_color_t *fg);

lv_style_t *ui_style_card(void);
lv_style_t *ui_style_pill(void);

/* Full-screen transparent container for one view; the active screen's only
 * child. Slides in briefly when `animate` is set. */
lv_obj_t *ui_view_create(bool animate);

/* Shrink slightly while pressed. Works on any clickable object. */
void ui_make_pressable(lv_obj_t *obj);

/* Grow and settle back: a short "pop" for good news. */
void ui_pop(lv_obj_t *obj);
