/*
 * Pack page: what is in a pack, and the choice to play it alone or to start
 * a battle with it against another Pukk.
 */
#include <stdio.h>
#include "esp_log.h"
#include "battle.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "pack";

#define TILE_SIZE       104
#define BUTTON_W        320

static char s_name[PACKS_NAME_MAX + 1];

static void play_async(void *arg)
{
    ui_go_game(s_name);
}

static void play_clicked_cb(lv_event_t *e)
{
    lv_async_call(play_async, NULL);
}

static void battle_clicked_cb(lv_event_t *e)
{
    /* The engine answers with a lobby event, which switches the view. */
    ESP_LOGI(TAG, "battle with %s", s_name);
    battle_host(s_name);
}

static void back_async(void *arg)
{
    ui_go_home();
}

static void back_clicked_cb(lv_event_t *e)
{
    lv_async_call(back_async, NULL);
}

bool pack_show(const char *pack_name)
{
    packs_entry_t info;
    packs_err_t err = packs_info(pack_name, &info);
    if (err != PACKS_OK) {
        ESP_LOGW(TAG, "cannot read %s: %s", pack_name, packs_err_name(err));
        return false;
    }
    snprintf(s_name, sizeof(s_name), "%s", info.name);

    ui_clean_screen();
    lv_obj_t *view = ui_view_create(true);

    lv_color_t fg;
    lv_color_t bg = ui_pack_color(info.name, &fg);
    lv_obj_t *tile = lv_obj_create(view);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, TILE_SIZE, TILE_SIZE);
    lv_obj_set_style_radius(tile, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 36);
    char initial[5];
    ui_initial(info.title, initial, sizeof(initial));
    lv_obj_center(ui_label(tile, UI_FONT_DISPLAY, fg, initial));

    lv_obj_t *title = ui_label(view, UI_FONT_TITLE, UI_COLOR_TEXT, info.title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 480 - 2 * 32);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 156);

    char text[32];
    snprintf(text, sizeof(text), "%u vragen", info.question_count);
    lv_obj_t *sub = ui_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, text);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 200);

    lv_obj_t *play = ui_button(view, "Spelen", UI_COLOR_BRAND, UI_COLOR_ON_BRAND, BUTTON_W, play_clicked_cb,
                               NULL);
    lv_obj_align(play, LV_ALIGN_TOP_MID, 0, 254);

    lv_obj_t *battle = ui_button(view, "Battle", UI_COLOR_OPPONENT, UI_COLOR_TEXT, BUTTON_W,
                                 battle_clicked_cb, NULL);
    lv_obj_align(battle, LV_ALIGN_TOP_MID, 0, 330);

    lv_obj_t *back = ui_button(view, "Terug", UI_COLOR_SURFACE, UI_COLOR_TEXT_DIM, 160, back_clicked_cb,
                               NULL);
    lv_obj_set_height(back, 48);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 412);
    return true;
}
