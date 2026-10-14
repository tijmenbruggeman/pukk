/*
 * Game view: the questions of one pack in order, each with its own timer,
 * then the score. Questions are read from flash one at a time.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "game";

#define BAR_RANGE       1000
#define REVEAL_MS       2000
#define TIMED_OUT       (-1)

/* Answer area: x 20..460, y 204..420. */
#define AREA_X          20
#define AREA_Y          204
#define AREA_W          440
#define AREA_H          216
#define AREA_GAP        12

static struct {
    bool active;
    char pack[PACKS_NAME_MAX + 1];
    char title[PACK_TITLE_MAX + 1];
    uint16_t count;
    uint16_t index;
    uint16_t score;
    bool answered;
    int bar_band;
    pack_question_t q;
    lv_obj_t *progress;
    lv_obj_t *score_label;
    lv_obj_t *question;
    lv_obj_t *bar;
    lv_obj_t *feedback;
    lv_obj_t *buttons[PACK_MAX_ANSWERS];
    lv_timer_t *next_timer;
} g;

static void show_question(void);

static void set_button_colors(lv_obj_t *btn, lv_color_t bg, lv_color_t text)
{
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), text, LV_PART_MAIN);
}

static void update_header(void)
{
    char text[24];
    snprintf(text, sizeof(text), "%u/%u", g.index + 1, g.count);
    lv_label_set_text(g.progress, text);
    snprintf(text, sizeof(text), "%u goed", g.score);
    lv_label_set_text(g.score_label, text);
}

/* --- result ----------------------------------------------------------------- */

static void go_home_async(void *arg)
{
    ui_go_home();
}

static void result_clicked_cb(lv_event_t *e)
{
    lv_async_call(go_home_async, NULL);
}

static void show_result(void)
{
    ESP_LOGI(TAG, "%s finished: %u of %u", g.pack, g.score, g.count);
    game_stop();
    lv_obj_t *scr = ui_clean_screen();

    /* Full-screen tap target; part of the view, so it goes away with it. */
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, result_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done = ui_label(page, UI_FONT_LARGE, UI_COLOR_ACCENT, "Klaar!");
    lv_obj_align(done, LV_ALIGN_CENTER, 0, -90);

    lv_obj_t *title = ui_label(page, UI_FONT_NORMAL, UI_COLOR_TEXT_DIM, g.title);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -36);

    char text[48];
    snprintf(text, sizeof(text), "%u van %u goed", g.score, g.count);
    lv_obj_t *score = ui_label(page, UI_FONT_NORMAL, UI_COLOR_TEXT, text);
    lv_obj_align(score, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *hint = ui_label(page, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, "Tik om terug te gaan");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 80);
}

/* --- question flow ---------------------------------------------------------- */

static void next_timer_cb(lv_timer_t *timer)
{
    g.next_timer = NULL; /* one-shot: LVGL deletes it after this call */
    if (++g.index < g.count) {
        show_question();
    } else {
        show_result();
    }
}

static void reveal(int chosen)
{
    g.answered = true;
    lv_anim_delete(g.bar, NULL);

    for (int i = 0; i < g.q.answer_count; i++) {
        if (i == g.q.correct) {
            set_button_colors(g.buttons[i], UI_COLOR_GOOD, UI_COLOR_TEXT);
        } else if (i == chosen) {
            set_button_colors(g.buttons[i], UI_COLOR_BAD, UI_COLOR_TEXT);
        } else {
            set_button_colors(g.buttons[i], UI_COLOR_BUTTON_DIM, UI_COLOR_TEXT_DIM);
        }
    }

    const char *text;
    lv_color_t color;
    if (chosen == g.q.correct) {
        g.score++;
        text = "Goed!";
        color = UI_COLOR_GOOD;
    } else if (chosen == TIMED_OUT) {
        text = "Tijd is om!";
        color = UI_COLOR_WARN;
    } else {
        text = "Fout!";
        color = UI_COLOR_BAD;
    }
    update_header();
    lv_label_set_text(g.feedback, text);
    lv_obj_set_style_text_color(g.feedback, color, LV_PART_MAIN);
    lv_obj_remove_flag(g.feedback, LV_OBJ_FLAG_HIDDEN);

    g.next_timer = lv_timer_create(next_timer_cb, REVEAL_MS, NULL);
    lv_timer_set_repeat_count(g.next_timer, 1);
}

static void answer_clicked_cb(lv_event_t *e)
{
    if (!g.answered) {
        reveal((int)(intptr_t)lv_event_get_user_data(e));
    }
}

static void timer_anim_cb(void *bar, int32_t value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);

    /* Only touch the style when the color band changes. */
    int band = value > BAR_RANGE / 2 ? 0 : value > BAR_RANGE / 5 ? 1 : 2;
    if (band != g.bar_band) {
        g.bar_band = band;
        lv_obj_set_style_bg_color(bar, band == 0 ? UI_COLOR_GOOD : band == 1 ? UI_COLOR_WARN
                                                                           : UI_COLOR_BAD,
                                  LV_PART_INDICATOR);
    }
}

static void timer_done_cb(lv_anim_t *a)
{
    if (!g.answered) {
        reveal(TIMED_OUT);
    }
}

/* 4 answers: 2 x 2 grid. 2 or 3 answers: full-width rows. */
static void layout_answers(uint8_t count)
{
    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        lv_obj_t *btn = g.buttons[i];
        if (i >= count) {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_HIDDEN);
        int32_t w, h, x, y;
        if (count == 4) {
            w = (AREA_W - 16) / 2;
            h = (AREA_H - 16) / 2;
            x = AREA_X + (i % 2) * (w + 16);
            y = AREA_Y + (i / 2) * (h + 16);
        } else {
            w = AREA_W;
            h = (AREA_H - AREA_GAP * (count - 1)) / count;
            x = AREA_X;
            y = AREA_Y + i * (h + AREA_GAP);
        }
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_width(lv_obj_get_child(btn, 0), w - 16);
    }
}

static void show_question(void)
{
    packs_err_t err = packs_read_question(g.pack, g.index, &g.q);
    if (err != PACKS_OK) {
        /* E.g. the pack was replaced with a shorter one during the game. */
        ESP_LOGW(TAG, "%s question %u: %s", g.pack, g.index, packs_err_name(err));
        g.count = g.index;
        show_result();
        return;
    }

    g.answered = false;
    update_header();
    lv_label_set_text(g.question, g.q.text);
    lv_obj_add_flag(g.feedback, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < g.q.answer_count; i++) {
        lv_label_set_text(lv_obj_get_child(g.buttons[i], 0), g.q.answers[i]);
        set_button_colors(g.buttons[i], UI_COLOR_BUTTON, UI_COLOR_TEXT);
    }
    layout_answers(g.q.answer_count);

    lv_anim_delete(g.bar, NULL);
    g.bar_band = -1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g.bar);
    lv_anim_set_exec_cb(&a, timer_anim_cb);
    lv_anim_set_values(&a, BAR_RANGE, 0);
    lv_anim_set_duration(&a, (uint32_t)g.q.time_s * 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, timer_done_cb);
    lv_anim_start(&a);
}

static void build_view(void)
{
    lv_obj_t *scr = ui_clean_screen();

    g.progress = ui_label(scr, UI_FONT_NORMAL, UI_COLOR_TEXT_DIM, "");
    lv_obj_align(g.progress, LV_ALIGN_TOP_LEFT, 20, 16);
    g.score_label = ui_label(scr, UI_FONT_NORMAL, UI_COLOR_ACCENT, "");
    lv_obj_align(g.score_label, LV_ALIGN_TOP_RIGHT, -20, 16);

    g.question = ui_label(scr, UI_FONT_NORMAL, UI_COLOR_TEXT, "");
    lv_label_set_long_mode(g.question, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g.question, AREA_W);
    lv_obj_set_style_text_align(g.question, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(g.question, LV_ALIGN_TOP_MID, 0, 56);

    g.bar = lv_bar_create(scr);
    lv_obj_set_size(g.bar, AREA_W, 10);
    lv_obj_align(g.bar, LV_ALIGN_TOP_MID, 0, 180);
    lv_bar_set_range(g.bar, 0, BAR_RANGE);
    lv_obj_set_style_bg_color(g.bar, UI_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g.bar, LV_OPA_COVER, LV_PART_MAIN);

    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, answer_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *label = ui_label(btn, UI_FONT_NORMAL, UI_COLOR_TEXT, "");
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(label);
        g.buttons[i] = btn;
    }

    g.feedback = ui_label(scr, UI_FONT_NORMAL, UI_COLOR_TEXT, "");
    lv_obj_align(g.feedback, LV_ALIGN_BOTTOM_MID, 0, -30);
}

bool game_start(const char *pack_name)
{
    packs_entry_t info;
    packs_err_t err = packs_info(pack_name, &info);
    if (err != PACKS_OK) {
        ESP_LOGW(TAG, "cannot play %s: %s", pack_name, packs_err_name(err));
        return false;
    }

    memset(&g, 0, sizeof(g));
    g.active = true;
    snprintf(g.pack, sizeof(g.pack), "%s", info.name);
    snprintf(g.title, sizeof(g.title), "%s", info.title);
    g.count = info.question_count;
    ESP_LOGI(TAG, "playing %s (\"%s\", %u questions)", g.pack, g.title, g.count);

    build_view();
    show_question();
    return true;
}

void game_stop(void)
{
    if (!g.active) {
        return;
    }
    if (g.next_timer) {
        lv_timer_delete(g.next_timer);
        g.next_timer = NULL;
    }
    lv_anim_delete(g.bar, NULL);
    g.active = false;
}
