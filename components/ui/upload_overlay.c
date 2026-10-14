/*
 * Pop-up on LVGL's top layer for pack uploads and short messages. It sits
 * above whatever view is active and disappears by itself when done.
 */
#include <stdio.h>
#include "pukk_ble.h"
#include "packs.h"
#include "ui_priv.h"

#define HIDE_AFTER_MS   2500

static lv_obj_t *s_box;
static lv_obj_t *s_text;
static lv_obj_t *s_bar;
static lv_timer_t *s_hide_timer;

static void hide_timer_cb(lv_timer_t *timer)
{
    s_hide_timer = NULL; /* one-shot */
    lv_obj_delete(s_box);
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
    s_box = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_box, 400, 160);
    lv_obj_center(s_box);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_box, UI_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_box, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_box, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_box, 20, LV_PART_MAIN);

    s_text = ui_label(s_box, UI_FONT_NORMAL, UI_COLOR_TEXT, "");
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_text, LV_PCT(100));
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_text, LV_ALIGN_TOP_MID, 0, 4);

    s_bar = lv_bar_create(s_box);
    lv_obj_set_size(s_bar, LV_PCT(100), 12);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_BUTTON, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
}

static void hide_later(void)
{
    s_hide_timer = lv_timer_create(hide_timer_cb, HIDE_AFTER_MS, NULL);
    lv_timer_set_repeat_count(s_hide_timer, 1);
}

void overlay_upload_progress(const char *name, uint32_t received, uint32_t total)
{
    ensure_box();
    char text[64];
    snprintf(text, sizeof(text), "Pack ontvangen\n%s", name);
    lv_label_set_text(s_text, text);
    lv_obj_set_style_text_color(s_text, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_bar, total ? (int32_t)((uint64_t)received * 100 / total) : 0, LV_ANIM_OFF);
}

static const char *result_text(int result)
{
    switch (result) {
    case PUKK_BLE_RESULT_DISCONNECTED:  return "verbinding verbroken";
    case PUKK_BLE_RESULT_ABORTED:       return "afgebroken";
    case PACKS_ERR_NO_SPACE:            return "geen ruimte meer";
    case PACKS_ERR_MISMATCH:            return "data beschadigd";
    case PACKS_ERR_INVALID_PACK:        return "geen geldig pack";
    default:                            return "fout";
    }
}

void overlay_upload_finished(const char *name, int result)
{
    ensure_box();
    char text[96];
    if (result == 0) {
        snprintf(text, sizeof(text), "Opgeslagen\n%s", name);
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
    } else {
        snprintf(text, sizeof(text), "Upload mislukt\n%s", result_text(result));
    }
    lv_label_set_text(s_text, text);
    lv_obj_set_style_text_color(s_text, result == 0 ? UI_COLOR_GOOD : UI_COLOR_BAD, LV_PART_MAIN);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    hide_later();
}

void overlay_message(const char *text, lv_color_t color)
{
    ensure_box();
    lv_label_set_text(s_text, text);
    lv_obj_set_style_text_color(s_text, color, LV_PART_MAIN);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    hide_later();
}
