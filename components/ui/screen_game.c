/*
 * Solo game: the questions of one pack in order, each with its own timer,
 * then the result. Questions are read from flash one at a time; the question
 * screen itself is quiz_view.c.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "game";

#define REVEAL_MS       2200
#define TIMED_OUT       (-1)

static struct {
    bool active;
    char pack[PACKS_NAME_MAX + 1];
    char title[PACK_TITLE_MAX + 1];
    uint16_t count;
    uint16_t index;
    uint16_t score;
    pack_question_t q;
    lv_obj_t *progress;
    lv_obj_t *score_label;
    lv_timer_t *next_timer;
} g;

static void show_question(void);

static void update_header(void)
{
    char text[24];
    snprintf(text, sizeof(text), "%u/%u", g.index + 1, g.count);
    lv_label_set_text(g.progress, text);
    snprintf(text, sizeof(text), UI_SYMBOL_STAR " %u", g.score);
    lv_label_set_text(g.score_label, text);
}

/* --- result ----------------------------------------------------------------- */

static void go_home_async(void *arg)
{
    ui_go_home();
}

static void back_clicked_cb(lv_event_t *e)
{
    lv_async_call(go_home_async, NULL);
}

static const char *result_message(uint16_t score, uint16_t count)
{
    if (count > 0 && score == count) {
        return "Foutloos!";
    } else if (score * 3 >= count * 2) {
        return "Goed gedaan!";
    } else if (score * 3 >= count) {
        return "Niet slecht!";
    }
    return "Volgende keer beter!";
}

static void show_result(void)
{
    ESP_LOGI(TAG, "%s finished: %u of %u", g.pack, g.score, g.count);
    game_stop();
    ui_clean_screen();
    lv_obj_t *view = ui_view_create(true);

    lv_obj_t *done = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_BRAND, "Klaar!");
    lv_obj_align(done, LV_ALIGN_TOP_MID, 0, 36);

    /* One star for a third, two for two thirds, three for everything right. */
    int stars = 0;
    if (g.count > 0) {
        stars = (g.score == g.count) ? 3 : (g.score * 3 >= g.count * 2) ? 2 : (g.score * 3 >= g.count) ? 1 : 0;
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_t *star = ui_label(view, UI_FONT_TITLE, i < stars ? UI_COLOR_BRAND : UI_COLOR_SURFACE_2,
                                  UI_SYMBOL_STAR);
        lv_obj_align(star, LV_ALIGN_TOP_MID, (i - 1) * 56, 118 - (i == 1 ? 12 : 0));
        if (i < stars) {
            ui_pop(star);
        }
    }

    char text[48];
    snprintf(text, sizeof(text), "%u van %u goed", g.score, g.count);
    lv_obj_t *score = ui_label(view, UI_FONT_QUESTION, UI_COLOR_TEXT, text);
    lv_obj_align(score, LV_ALIGN_TOP_MID, 0, 186);

    lv_obj_t *message = ui_label(view, UI_FONT_BODY, UI_COLOR_TEXT_DIM, result_message(g.score, g.count));
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 232);

    lv_obj_t *title = ui_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, g.title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 272);

    lv_obj_t *back = ui_button(view, "Terug", UI_COLOR_BRAND, UI_COLOR_ON_BRAND, 220, back_clicked_cb, NULL);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -40);
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
    quiz_reveal(g.q.correct, chosen);

    if (chosen == g.q.correct) {
        g.score++;
        quiz_feedback(UI_SYMBOL_OK " Goed!", UI_COLOR_GOOD, UI_COLOR_ON_GOOD);
    } else if (chosen == TIMED_OUT) {
        quiz_feedback("Tijd is om!", UI_COLOR_WARN, UI_COLOR_ON_WARN);
    } else {
        quiz_feedback(UI_SYMBOL_CLOSE " Fout", UI_COLOR_BAD, UI_COLOR_TEXT);
    }
    update_header();

    g.next_timer = lv_timer_create(next_timer_cb, REVEAL_MS, NULL);
    lv_timer_set_repeat_count(g.next_timer, 1);
}

static void on_answer(int choice)
{
    reveal(choice);
}

static void on_timeout(void)
{
    reveal(TIMED_OUT);
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
    update_header();
    quiz_show(&g.q);
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

    ui_clean_screen();
    lv_obj_t *view = ui_view_create(true);
    lv_obj_t *progress = ui_pill(view, &g.progress, UI_COLOR_TEXT);
    lv_obj_align(progress, LV_ALIGN_TOP_LEFT, UI_SCREEN_PAD, 26);
    lv_obj_t *score = ui_pill(view, &g.score_label, UI_COLOR_BRAND);
    lv_obj_align(score, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD, 26);
    quiz_build(view, on_answer, on_timeout);

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
    quiz_stop();
    g.active = false;
}
