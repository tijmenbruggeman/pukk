/*
 * Public UI entry points (thread-safe) and switching between views.
 */
#include <string.h>
#include "ui.h"
#include "ui_priv.h"

typedef enum {
    VIEW_NONE,
    VIEW_HOME,
    VIEW_GAME,
} view_t;

static view_t s_view = VIEW_NONE;
static char s_device_name[24];

lv_obj_t *ui_clean_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    /* Black is "pixels off" on AMOLED: looks best and saves power. */
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

const char *ui_device_name(void)
{
    return s_device_name;
}

void ui_go_home(void)
{
    if (s_view == VIEW_GAME) {
        game_stop();
    }
    home_show();
    s_view = VIEW_HOME;
}

void ui_go_game(const char *pack_name)
{
    home_forget();
    if (game_start(pack_name)) {
        s_view = VIEW_GAME;
    } else {
        overlay_message("Dit pack kan niet\ngeladen worden", UI_COLOR_BAD);
        home_show();
        s_view = VIEW_HOME;
    }
}

void ui_show_home(void)
{
    if (ui_lock(-1)) {
        ui_go_home();
        ui_unlock();
    }
}

void ui_back(void)
{
    if (ui_lock(-1)) {
        if (s_view == VIEW_GAME) {
            ui_go_home();
        }
        ui_unlock();
    }
}

void ui_set_device_name(const char *name)
{
    if (ui_lock(-1)) {
        strncpy(s_device_name, name, sizeof(s_device_name) - 1);
        if (s_view == VIEW_HOME) {
            home_update_device_name();
        }
        ui_unlock();
    }
}

void ui_packs_changed(void)
{
    if (ui_lock(-1)) {
        if (s_view == VIEW_HOME) {
            home_refresh();
        }
        ui_unlock();
    }
}

void ui_upload_progress(const char *name, uint32_t received, uint32_t total)
{
    if (ui_lock(-1)) {
        overlay_upload_progress(name, received, total);
        ui_unlock();
    }
}

void ui_upload_finished(const char *name, int result)
{
    if (ui_lock(-1)) {
        overlay_upload_finished(name, result);
        ui_unlock();
    }
}
