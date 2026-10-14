/*
 * Public UI entry points (thread-safe), device status, and switching between
 * views.
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_priv.h"

#define SPLASH_MIN_MS   1400

typedef enum {
    VIEW_NONE,
    VIEW_SPLASH,
    VIEW_HOME,
    VIEW_PACK,
    VIEW_GAME,
    VIEW_BATTLE,
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

lv_obj_t *ui_pill(lv_obj_t *parent, lv_obj_t **label, lv_color_t color)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_add_style(pill, ui_style_pill(), LV_PART_MAIN);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    *label = ui_label(pill, UI_FONT_SMALL, color, "");
    return pill;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, int32_t width,
                    lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, ui_style_pill(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    lv_obj_set_size(button, width, UI_BUTTON_H);
    ui_make_pressable(button);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_center(ui_label(button, UI_FONT_BODY, fg, text));
    return button;
}

void ui_initial(const char *title, char *out, size_t size)
{
    size_t len = 1;
    unsigned char c = (unsigned char)title[0];
    if (c >= 0xF0) {
        len = 4;
    } else if (c >= 0xE0) {
        len = 3;
    } else if (c >= 0xC0) {
        len = 2;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(out, title, len);
    out[len] = '\0';
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

/* Let the current view stop its timers before the screen is cleaned. */
static void leave_view(void)
{
    if (s_splash_timer) {
        lv_timer_delete(s_splash_timer);
        s_splash_timer = NULL;
    }
    switch (s_view) {
    case VIEW_HOME:
        home_forget();
        break;
    case VIEW_GAME:
        game_stop();
        break;
    case VIEW_BATTLE:
        battle_view_stop();
        break;
    default:
        break;
    }
    s_view = VIEW_NONE;
}

void ui_go_home(void)
{
    leave_view();
    home_show();
    s_view = VIEW_HOME;
}

static void cannot_load(void)
{
    overlay_message("Dit pack kan niet\ngeladen worden", UI_COLOR_BAD);
    ui_go_home();
}

void ui_go_pack(const char *pack_name)
{
    leave_view();
    if (pack_show(pack_name)) {
        s_view = VIEW_PACK;
    } else {
        cannot_load();
    }
}

void ui_go_game(const char *pack_name)
{
    leave_view();
    if (game_start(pack_name)) {
        s_view = VIEW_GAME;
    } else {
        cannot_load();
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
        if (s_view == VIEW_GAME || s_view == VIEW_PACK) {
            ui_go_home();
        } else if (s_view == VIEW_BATTLE) {
            /* The engine answers with STOPPED, which brings us home. */
            battle_leave();
        }
        ui_unlock();
    }
}

static const char *stop_message(const battle_event_t *event, char *buf, size_t size)
{
    switch (event->stopped.reason) {
    case BATTLE_STOP_LEFT:
        return NULL;
    case BATTLE_STOP_OPPONENT:
        snprintf(buf, size, "%s is gestopt", event->opponent[0] ? event->opponent : "De ander");
        return buf;
    case BATTLE_STOP_LINK:
        return "Verbinding verbroken";
    case BATTLE_STOP_VERSION:
        return "Andere Pukk-versie:\nupdate ze allebei";
    case BATTLE_STOP_TRANSFER:
        return "Pack versturen\nmislukt";
    case BATTLE_STOP_PACK:
        return "Dit pack kan niet\ngeladen worden";
    }
    return NULL;
}

void ui_battle_event(const battle_event_t *event)
{
    if (!ui_lock(-1)) {
        return;
    }
    if (event->type == BATTLE_EVENT_STOPPED) {
        char buf[48];
        const char *message = stop_message(event, buf, sizeof(buf));
        if (message) {
            overlay_message(message, UI_COLOR_BAD);
        }
        if (s_view == VIEW_BATTLE || s_view == VIEW_PACK) {
            ui_go_home();
        }
    } else {
        if (s_view != VIEW_BATTLE) {
            leave_view();
            s_view = VIEW_BATTLE;
        }
        battle_view_event(event);
    }
    ui_unlock();
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
