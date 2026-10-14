/*
 * Pop-up on LVGL's top layer for pack uploads and short messages. It sits
 * above whatever view is active and disappears by itself when done.
 */
#include <stdio.h>
#include "pukk_ble.h"
#include "packs.h"
#include "ui_priv.h"

#define HIDE_AFTER_MS   2500
#define BOX_W           400
#define BOX_H           172

static lv_obj_t *s_scrim;
static lv_obj_t *s_box;
static lv_obj_t *s_title;
static lv_obj_t *s_detail;
static lv_obj_t *s_bar;
static lv_timer_t *s_hide_timer;

static void hide_timer_cb(lv_timer_t *timer)
{
    s_hide_timer = NULL; /* one-shot */
    lv_obj_delete(s_scrim); /* the box is its child */
    s_scrim = NULL;
    s_box = NULL;
}

static void ensure_box(void)
{
    if (s_hide_timer) {
        lv_timer_delete(s_hide_timer);
        s_hide_timer = NULL;
    }
    if (s_box) {
        return;
    }
    ui_theme_init();
    /* Dim what is behind and swallow taps, so nothing starts mid-upload. */
    s_scrim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_scrim, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_60, LV_PART_MAIN);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);

    s_box = lv_obj_create(s_scrim);
    lv_obj_remove_style_all(s_box);
    lv_obj_add_style(s_box, ui_style_card(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_box, UI_COLOR_BRAND, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_box, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_box, 28, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_box, 22, LV_PART_MAIN);
    lv_obj_set_size(s_box, BOX_W, BOX_H);
    lv_obj_center(s_box);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    ui_pop(s_box);

    s_title = ui_label(s_box, UI_FONT_TITLE, UI_COLOR_TEXT, ""); /* has symbols */
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 0);

    s_detail = ui_label(s_box, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, "");
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_detail, LV_PCT(100));
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_detail, LV_ALIGN_TOP_MID, 0, 44);

    s_bar = lv_bar_create(s_box);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, LV_PCT(100), 14);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_radius(s_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_BRAND, LV_PART_INDICATOR);
}

static void hide_later(void)
{
    s_hide_timer = lv_timer_create(hide_timer_cb, HIDE_AFTER_MS, NULL);
    lv_timer_set_repeat_count(s_hide_timer, 1);
}

static void set_text(const char *title, lv_color_t title_color, const char *detail)
{
    lv_label_set_text(s_title, title);
    lv_obj_set_style_text_color(s_title, title_color, LV_PART_MAIN);
    lv_label_set_text(s_detail, detail);
}

void overlay_upload_progress(const char *name, uint32_t received, uint32_t total)
{
    ensure_box();
    int percent = total ? (int)((uint64_t)received * 100 / total) : 0;
    char detail[64];
    snprintf(detail, sizeof(detail), "%s  ·  %d%%", name, percent);
    set_text("Pack ontvangen", UI_COLOR_TEXT, detail);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_bar, percent, LV_ANIM_OFF);
}

static const char *result_text(int result)
{
    switch (result) {
    case PUKK_BLE_RESULT_DISCONNECTED:  return "Verbinding verbroken";
    case PUKK_BLE_RESULT_ABORTED:       return "Afgebroken";
    case PACKS_ERR_NO_SPACE:            return "Geen ruimte meer";
    case PACKS_ERR_MISMATCH:            return "Data beschadigd";
    case PACKS_ERR_INVALID_PACK:        return "Geen geldig pack";
    default:                            return "Er ging iets mis";
    }
}

void overlay_upload_finished(const char *name, int result)
{
    ensure_box();
    if (result == 0) {
        set_text(UI_SYMBOL_OK " Opgeslagen", UI_COLOR_GOOD, name);
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bar, UI_COLOR_GOOD, LV_PART_INDICATOR);
    } else {
        set_text(UI_SYMBOL_CLOSE " Upload mislukt", UI_COLOR_BAD, result_text(result));
        lv_obj_set_style_bg_color(s_bar, UI_COLOR_BAD, LV_PART_INDICATOR);
    }
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    hide_later();
}

void overlay_message(const char *text, lv_color_t color)
{
    ensure_box();
    set_text(text, color, "");
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    hide_later();
}
