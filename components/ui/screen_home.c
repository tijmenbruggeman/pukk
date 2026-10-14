/*
 * Home view: the packs on the device. Tap one to play it.
 */
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "home";

#define LIST_MAX    32

static lv_obj_t *s_list;
static lv_obj_t *s_name_label;
static char s_names[LIST_MAX][PACKS_NAME_MAX + 1];
static char s_selected[PACKS_NAME_MAX + 1];

/* Switch views outside the click event: it deletes the button that was clicked. */
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

static void add_pack_button(int index, const packs_entry_t *pack)
{
    lv_obj_t *btn = lv_button_create(s_list);
    lv_obj_set_size(btn, LV_PCT(100), 76);
    lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BUTTON, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, pack_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *title = ui_label(btn, UI_FONT_NORMAL, UI_COLOR_TEXT, pack->title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);

    char info[32];
    snprintf(info, sizeof(info), "%u vragen", pack->question_count);
    lv_obj_t *sub = ui_label(btn, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, info);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 4, -2);

    snprintf(s_names[index], sizeof(s_names[index]), "%s", pack->name);
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
        add_pack_button((int)i, &packs[i]);
    }
    free(packs);

    if (count == 0) {
        lv_obj_t *empty = ui_label(s_list, UI_FONT_NORMAL, UI_COLOR_TEXT_DIM,
                                   "Nog geen packs.\n\nUpload er een via Bluetooth met tools/pukk.py.");
        lv_label_set_long_mode(empty, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

void home_update_device_name(void)
{
    if (s_name_label) {
        lv_label_set_text(s_name_label, ui_device_name());
    }
}

void home_show(void)
{
    lv_obj_t *scr = ui_clean_screen();

    lv_obj_t *title = ui_label(scr, UI_FONT_LARGE, UI_COLOR_ACCENT, "Pukk");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    s_name_label = ui_label(scr, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, ui_device_name());
    lv_obj_align(s_name_label, LV_ALIGN_TOP_MID, 0, 62);

    /* Scrollable column of packs; ends above the performance overlays. */
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 440, 340);
    lv_obj_set_pos(s_list, 20, 92);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    home_refresh();
}

void home_forget(void)
{
    s_list = NULL;
    s_name_label = NULL;
}
