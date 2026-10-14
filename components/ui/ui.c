/*
 * Public UI entry points (thread-safe), device status, and switching between
 * views.
 */
#include <string.h>
#include "ui.h"
#include "ui_priv.h"

#define SPLASH_MIN_MS   1400

typedef enum {
    VIEW_NONE,
    VIEW_SPLASH,
    VIEW_HOME,
    VIEW_GAME,
} view_t;

static view_t s_view = VIEW_NONE;
static uint32_t s_splash_start;
static lv_timer_t *s_splash_timer;
static ui_status_t s_status = { .battery_percent = -1 };

const ui_status_t *ui_status(void)
{
    return &s_status;
}

void ui_clean_screen(void)
{
    ui_theme_init();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    /* Black is "pixels off" on AMOLED: looks best and saves power. */
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(scr, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

void ui_go_home(void)
{
    if (s_splash_timer) {
        lv_timer_delete(s_splash_timer);
        s_splash_timer = NULL;
    }
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

static void splash_done_cb(lv_timer_t *timer)
{
    s_splash_timer = NULL; /* one-shot */
    ui_go_home();
}

void ui_show_splash(void)
{
    if (ui_lock(-1)) {
        splash_show();
        s_view = VIEW_SPLASH;
        s_splash_start = lv_tick_get();
        ui_unlock();
    }
}

void ui_show_home(void)
{
    if (!ui_lock(-1)) {
        return;
    }
    /* Let the splash screen stay up long enough to be seen. */
    uint32_t shown = lv_tick_elaps(s_splash_start);
    if (s_view == VIEW_SPLASH && shown < SPLASH_MIN_MS) {
        if (!s_splash_timer) {
            s_splash_timer = lv_timer_create(splash_done_cb, SPLASH_MIN_MS - shown, NULL);
            lv_timer_set_repeat_count(s_splash_timer, 1);
        }
    } else {
        ui_go_home();
    }
    ui_unlock();
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

static void status_changed(void)
{
    if (s_view == VIEW_HOME) {
        home_update_status();
    }
}

void ui_set_device_name(const char *name)
{
    if (ui_lock(-1)) {
        strncpy(s_status.device_name, name, sizeof(s_status.device_name) - 1);
        status_changed();
        ui_unlock();
    }
}

void ui_set_battery(int percent, bool charging)
{
    if (ui_lock(-1)) {
        s_status.battery_percent = percent;
        s_status.charging = charging;
        status_changed();
        ui_unlock();
    }
}

void ui_set_ble_connected(bool connected)
{
    if (ui_lock(-1)) {
        s_status.ble_connected = connected;
        status_changed();
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
