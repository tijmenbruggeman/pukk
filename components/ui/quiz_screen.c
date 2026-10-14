/*
 * Phase 0 test screen: title, one question with four answer buttons, a timer
 * bar that drains in 15 s, and right/wrong feedback.
 *
 * On-screen text is Dutch. The built-in Montserrat fonts are ASCII only, so
 * avoid accented characters for now.
 */
#include <stdint.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"

static const char *TAG = "quiz";

#define QUESTION_TIME_MS    15000
#define BAR_RANGE           1000
#define ANSWER_COUNT        4
#define TIMED_OUT           (-1)

#define COLOR_ACCENT        lv_color_hex(0xFFC233)
#define COLOR_TEXT          lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM      lv_color_hex(0x6B6B70)
#define COLOR_BUTTON        lv_color_hex(0x26262B)
#define COLOR_BUTTON_DIM    lv_color_hex(0x151517)
#define COLOR_TRACK         lv_color_hex(0x1E1E22)
#define COLOR_GOOD          lv_color_hex(0x22C55E)
#define COLOR_WARN          lv_color_hex(0xF59E0B)
#define COLOR_BAD           lv_color_hex(0xEF4444)

typedef struct {
    const char *text;
    const char *answers[ANSWER_COUNT];
    uint8_t correct;
} question_t;

static const question_t s_question = {
    .text = "Welke planeet staat het dichtst bij de zon?",
    .answers = { "Venus", "Mercurius", "Mars", "Aarde" },
    .correct = 1,
};

static lv_obj_t *s_bar;
static lv_obj_t *s_feedback;
static lv_obj_t *s_buttons[ANSWER_COUNT];
static bool s_answered;
static int s_bar_band; /* 0 = green, 1 = orange, 2 = red, -1 = not set */

static void set_button_colors(lv_obj_t *btn, lv_color_t bg, lv_color_t text)
{
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), text, LV_PART_MAIN);
}

static void set_bar_color(lv_color_t color)
{
    lv_obj_set_style_bg_color(s_bar, color, LV_PART_INDICATOR);
}

static void reveal(int chosen)
{
    s_answered = true;
    lv_anim_delete(s_bar, NULL);

    for (int i = 0; i < ANSWER_COUNT; i++) {
        if (i == s_question.correct) {
            set_button_colors(s_buttons[i], COLOR_GOOD, COLOR_TEXT);
        } else if (i == chosen) {
            set_button_colors(s_buttons[i], COLOR_BAD, COLOR_TEXT);
        } else {
            set_button_colors(s_buttons[i], COLOR_BUTTON_DIM, COLOR_TEXT_DIM);
        }
    }

    const char *text;
    lv_color_t color;
    if (chosen == s_question.correct) {
        text = "Goed!";
        color = COLOR_GOOD;
    } else if (chosen == TIMED_OUT) {
        text = "Tijd is om!";
        color = COLOR_WARN;
    } else {
        text = "Fout!";
        color = COLOR_BAD;
    }
    lv_label_set_text(s_feedback, text);
    lv_obj_set_style_text_color(s_feedback, color, LV_PART_MAIN);
    lv_obj_remove_flag(s_feedback, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "answer %d -> %s", chosen, text);
}

static void answer_clicked_cb(lv_event_t *e)
{
    if (!s_answered) {
        reveal((int)(intptr_t)lv_event_get_user_data(e));
    }
}

static void timer_anim_cb(void *bar, int32_t value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);

    /* Only touch the style when the color band changes. */
    int band = value > BAR_RANGE / 2 ? 0 : value > BAR_RANGE / 5 ? 1 : 2;
    if (band != s_bar_band) {
        s_bar_band = band;
        set_bar_color(band == 0 ? COLOR_GOOD : band == 1 ? COLOR_WARN : COLOR_BAD);
    }
}

static void timer_done_cb(lv_anim_t *a)
{
    if (!s_answered) {
        reveal(TIMED_OUT);
    }
}

static void start_question(void)
{
    s_answered = false;
    lv_obj_add_flag(s_feedback, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ANSWER_COUNT; i++) {
        set_button_colors(s_buttons[i], COLOR_BUTTON, COLOR_TEXT);
    }

    lv_anim_delete(s_bar, NULL);
    s_bar_band = -1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_exec_cb(&a, timer_anim_cb);
    lv_anim_set_values(&a, BAR_RANGE, 0);
    lv_anim_set_duration(&a, QUESTION_TIME_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, timer_done_cb);
    lv_anim_start(&a);
}

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    return label;
}

static void build_screen(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    /* Black is "pixels off" on AMOLED: looks best and saves power. */
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(scr, &lv_font_montserrat_40, COLOR_ACCENT);
    lv_label_set_text(title, "Pukk");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *question = create_label(scr, &lv_font_montserrat_24, COLOR_TEXT);
    lv_label_set_long_mode(question, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(question, 440);
    lv_obj_set_style_text_align(question, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(question, s_question.text);
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 76);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 440, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 156);
    lv_bar_set_range(s_bar, 0, BAR_RANGE);
    lv_obj_set_style_bg_color(s_bar, COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);

    /* 2 x 2 answer grid, 440 px wide. */
    for (int i = 0; i < ANSWER_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_size(btn, 212, 100);
        lv_obj_set_pos(btn, 20 + (i % 2) * 228, 188 + (i / 2) * 116);
        lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, answer_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = create_label(btn, &lv_font_montserrat_24, COLOR_TEXT);
        lv_label_set_text(label, s_question.answers[i]);
        lv_obj_center(label);
        s_buttons[i] = btn;
    }

    s_feedback = create_label(scr, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_align(s_feedback, LV_ALIGN_BOTTOM_MID, 0, -34);
}

void ui_quiz_show(void)
{
    if (!ui_lock(-1)) {
        return;
    }
    build_screen(lv_screen_active());
    start_question();
    ui_unlock();
}

void ui_quiz_restart(void)
{
    if (!ui_lock(-1)) {
        return;
    }
    if (s_bar) {
        ESP_LOGI(TAG, "restart question");
        start_question();
    }
    ui_unlock();
}
