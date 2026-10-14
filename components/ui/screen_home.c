/*
 * Home view: status bar, wordmark and the packs on the device. Tap a pack to
 * play it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "home";

#define LIST_MAX    32
#define LIST_Y      112
#define CARD_H      84
#define TILE_SIZE   56

static lv_obj_t *s_list;
static lv_obj_t *s_name_label;
static lv_obj_t *s_ble_label;
static lv_obj_t *s_battery_label;
static char s_names[LIST_MAX][PACKS_NAME_MAX + 1];
static char s_selected[PACKS_NAME_MAX + 1];

static const char *battery_symbol(int percent)
{
    if (percent >= 90) {
        return LV_SYMBOL_BATTERY_FULL;
    } else if (percent >= 65) {
        return LV_SYMBOL_BATTERY_3;
    } else if (percent >= 40) {
        return LV_SYMBOL_BATTERY_2;
    } else if (percent >= 15) {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

void home_update_status(void)
{
    if (!s_name_label) {
        return;
    }
    const ui_status_t *st = ui_status();
    lv_label_set_text(s_name_label, st->device_name);

    char text[32];
    lv_color_t color = UI_COLOR_TEXT_DIM;
    if (st->battery_percent < 0) {
        snprintf(text, sizeof(text), LV_SYMBOL_CHARGE);   /* USB power, no battery */
    } else {
        snprintf(text, sizeof(text), "%s%s  %d%%", st->charging ? LV_SYMBOL_CHARGE " " : "",
                 battery_symbol(st->battery_percent), st->battery_percent);
        if (st->battery_percent < 15 && !st->charging) {
            color = UI_COLOR_BAD;
        }
    }
    lv_label_set_text(s_battery_label, text);
    lv_obj_set_style_text_color(s_battery_label, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ble_label, st->ble_connected ? UI_COLOR_BLE : UI_COLOR_TEXT_DIM,
                                LV_PART_MAIN);
    lv_obj_align_to(s_ble_label, s_battery_label, LV_ALIGN_OUT_LEFT_MID, -14, 0);
}

/* Switch views outside the click event: it deletes the card that was clicked. */
static void start_game_async(void *arg)
{
    ui_go_game(s_selected);
}

static void pack_clicked_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    snprintf(s_selected, sizeof(s_selected), "%s", s_names[index]);
    ESP_LOGI(TAG, "start pack %s", s_selected);
    lv_async_call(start_game_async, NULL);
}

/* First character of the title (whole UTF-8 sequence), upper-cased if ASCII. */
static void initial_of(const char *title, char *out, size_t size)
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

static void add_pack_card(int index, const packs_entry_t *pack)
{
    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, ui_style_card(), LV_PART_MAIN);
    lv_obj_set_size(card, LV_PCT(100), CARD_H);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    ui_make_pressable(card);
    lv_obj_add_event_cb(card, pack_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_color_t fg;
    lv_color_t bg = ui_pack_color(pack->name, &fg);
    lv_obj_t *tile = lv_obj_create(card);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, TILE_SIZE, TILE_SIZE);
    lv_obj_set_style_radius(tile, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(tile, LV_ALIGN_LEFT_MID, 0, 0);
    char initial[5];
    initial_of(pack->title, initial, sizeof(initial));
    lv_obj_center(ui_label(tile, UI_FONT_QUESTION, fg, initial));

    const int32_t text_x = TILE_SIZE + 14;
    lv_obj_t *title = ui_label(card, UI_FONT_BODY, UI_COLOR_TEXT, pack->title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 448 - 24 - text_x - 32);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, text_x, 4);

    char info[32];
    snprintf(info, sizeof(info), "%u vragen", pack->question_count);
    lv_obj_t *sub = ui_label(card, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, info);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, text_x, -4);

    lv_obj_t *chevron = ui_label(card, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, LV_SYMBOL_RIGHT);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -4, 0);

    snprintf(s_names[index], sizeof(s_names[index]), "%s", pack->name);
}

static void show_empty(void)
{
    lv_obj_t *box = lv_obj_create(s_list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(box, 60, LV_PART_MAIN);

    lv_obj_t *title = ui_label(box, UI_FONT_QUESTION, UI_COLOR_TEXT, "Nog geen packs");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    char text[96];
    snprintf(text, sizeof(text), "Upload er een via Bluetooth\nnaar %s",
             ui_status()->device_name[0] ? ui_status()->device_name : "dit apparaat");
    lv_obj_t *body = ui_label(box, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, text);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 48);
}

void home_refresh(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);

    packs_entry_t *packs = malloc(LIST_MAX * sizeof(packs_entry_t));
    size_t count = packs ? packs_list(packs, LIST_MAX) : 0;
    for (size_t i = 0; i < count; i++) {
        add_pack_card((int)i, &packs[i]);
    }
    free(packs);
    if (count == 0) {
        show_empty();
    }
}

void home_show(void)
{
    ui_clean_screen();
    lv_obj_t *view = ui_view_create(true);

    /* Status bar */
    s_name_label = ui_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, "");
    lv_obj_align(s_name_label, LV_ALIGN_TOP_LEFT, UI_SCREEN_PAD + 4, 12);
    s_battery_label = ui_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, "");
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD - 4, 12);
    s_ble_label = ui_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, LV_SYMBOL_BLUETOOTH);

    lv_obj_t *logo = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_BRAND, "pukk");
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 38);

    /* Scrollable column of packs. */
    s_list = lv_obj_create(view);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 480 - 2 * UI_SCREEN_PAD, 480 - LIST_Y);
    lv_obj_set_pos(s_list, UI_SCREEN_PAD, LIST_Y);
    lv_obj_set_style_pad_row(s_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_list, UI_SCREEN_PAD, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    home_update_status();
    home_refresh();
}

void home_forget(void)
{
    s_list = NULL;
    s_name_label = NULL;
    s_ble_label = NULL;
    s_battery_label = NULL;
}
