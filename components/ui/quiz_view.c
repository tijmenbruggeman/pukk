/*
 * Question screen shared by the solo game and battles: the timer ring (with a
 * feedback pill in its place once a question is decided), the question and
 * the answer tiles. The pills left and right in the header are the caller's.
 *
 * Layout (480 x 480):
 *   y  12- 76  header: timer ring or feedback in the middle
 *   y  84-220  question, centred vertically
 *   y 232-468  answer tiles: 2 x 2 grid (4), rows (3) or columns (2)
 */
#include <string.h>
#include "ui_priv.h"

#define TIMER_RANGE     1000
#define TIMER_SIZE      64

#define QUESTION_Y      84
#define QUESTION_H      136
#define AREA_X          UI_SCREEN_PAD
#define AREA_Y          232
#define AREA_W          (480 - 2 * UI_SCREEN_PAD)
#define AREA_H          (480 - AREA_Y - 12)
#define GAP             12
#define BADGE_SIZE      34
#define OTHER_SIZE      26

typedef struct {
    lv_obj_t *tile;
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_obj_t *badge;
    lv_obj_t *badge_label;
    lv_obj_t *other;    /* marks the opponent's choice */
} tile_t;

static struct {
    pack_question_t q;
    bool locked;
    int timer_band;
    int timer_seconds;
    quiz_answer_cb_t on_answer;
    quiz_timeout_cb_t on_timeout;
    lv_obj_t *timer;
    lv_obj_t *timer_label;
    lv_obj_t *feedback;
    lv_obj_t *feedback_label;
    lv_obj_t *question;
    tile_t tiles[PACK_MAX_ANSWERS];
} s;

static void show_badge(tile_t *t, const char *symbol, lv_color_t bg, lv_color_t fg)
{
    lv_obj_set_style_bg_color(t->badge, bg, LV_PART_MAIN);
    lv_label_set_text(t->badge_label, symbol);
    lv_obj_set_style_text_color(t->badge_label, fg, LV_PART_MAIN);
    lv_obj_remove_flag(t->badge, LV_OBJ_FLAG_HIDDEN);
}

static void set_tile_colors(tile_t *t, lv_color_t bg, lv_color_t fg)
{
    lv_obj_set_style_bg_color(t->tile, bg, LV_PART_MAIN);
    lv_obj_set_style_text_color(t->label, fg, LV_PART_MAIN);
    lv_obj_set_style_image_recolor(t->icon, fg, LV_PART_MAIN);
}

static void dim_tile(tile_t *t)
{
    set_tile_colors(t, UI_COLOR_SURFACE, UI_COLOR_TEXT_DIM);
}

static void answer_clicked_cb(lv_event_t *e)
{
    if (!s.locked && s.on_answer) {
        s.on_answer((int)(intptr_t)lv_event_get_user_data(e));
    }
}

static void timer_anim_cb(void *arc, int32_t value)
{
    lv_arc_set_value(arc, value);

    int seconds = (int)((value * s.q.time_s + TIMER_RANGE - 1) / TIMER_RANGE);
    if (seconds != s.timer_seconds) {
        s.timer_seconds = seconds;
        lv_label_set_text_fmt(s.timer_label, "%d", seconds);
    }
    /* Ring turns orange, then red, as time runs out. */
    int band = value > TIMER_RANGE / 2 ? 0 : value > TIMER_RANGE / 5 ? 1 : 2;
    if (band != s.timer_band) {
        s.timer_band = band;
        lv_color_t c = band == 0 ? UI_COLOR_BRAND : band == 1 ? UI_COLOR_WARN : UI_COLOR_BAD;
        lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    }
}

static void timer_done_cb(lv_anim_t *a)
{
    if (!s.locked) {
        s.locked = true;
        if (s.on_timeout) {
            s.on_timeout();
        }
    }
}

/* Place tile i of `count`: 4 = 2 x 2 grid, 3 = rows, 2 = columns. In rows and
 * the grid the shape sits left of the text; in columns it sits above it. */
static void layout_tile(tile_t *t, int i, int count)
{
    int32_t w, h, x, y;
    bool column = false;
    if (count == 4) {
        w = (AREA_W - GAP) / 2;
        h = (AREA_H - GAP) / 2;
        x = AREA_X + (i % 2) * (w + GAP);
        y = AREA_Y + (i / 2) * (h + GAP);
    } else if (count == 3) {
        w = AREA_W;
        h = (AREA_H - 2 * GAP) / 3;
        x = AREA_X;
        y = AREA_Y + i * (h + GAP);
    } else {
        w = (AREA_W - GAP) / 2;
        h = AREA_H;
        x = AREA_X + i * (w + GAP);
        y = AREA_Y;
        column = true;
    }
    lv_obj_set_pos(t->tile, x, y);
    lv_obj_set_size(t->tile, w, h);

    if (column) {
        lv_obj_align(t->icon, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_width(t->label, w - 24);
        lv_obj_set_style_text_align(t->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(t->label, LV_ALIGN_TOP_MID, 0, 100);
    } else {
        lv_obj_align(t->icon, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_set_width(t->label, w - 64 - 12);
        lv_obj_set_style_text_align(t->label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_align(t->label, LV_ALIGN_LEFT_MID, 64, 0);
    }
}

void quiz_show(const pack_question_t *q)
{
    s.q = *q;
    s.locked = false;
    lv_label_set_text(s.question, s.q.text);
    lv_obj_add_flag(s.feedback, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s.timer, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        tile_t *t = &s.tiles[i];
        if (i >= s.q.answer_count) {
            lv_obj_add_flag(t->tile, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ui_answer_style_t *st = ui_answer_style(i);
        lv_obj_remove_flag(t->tile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t->badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t->other, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(t->tile, 0, LV_PART_MAIN);
        lv_label_set_text(t->label, s.q.answers[i]);
        set_tile_colors(t, st->bg, st->fg);
        layout_tile(t, i, s.q.answer_count);
    }

    lv_anim_delete(s.timer, NULL);
    s.timer_band = -1;
    s.timer_seconds = -1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.timer);
    lv_anim_set_exec_cb(&a, timer_anim_cb);
    lv_anim_set_values(&a, TIMER_RANGE, 0);
    lv_anim_set_duration(&a, (uint32_t)s.q.time_s * 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, timer_done_cb);
    lv_anim_start(&a);
}

void quiz_lock(void)
{
    s.locked = true;
    lv_anim_delete(s.timer, NULL);
}

void quiz_mark_choice(int choice)
{
    s.locked = true; /* the timer keeps running until the result is in */
    for (int i = 0; i < s.q.answer_count; i++) {
        tile_t *t = &s.tiles[i];
        if (i == choice) {
            lv_obj_set_style_border_color(t->tile, UI_COLOR_TEXT, LV_PART_MAIN);
            lv_obj_set_style_border_width(t->tile, 4, LV_PART_MAIN);
        } else {
            dim_tile(t);
        }
    }
}

void quiz_reveal(int correct, int chosen)
{
    quiz_lock();
    for (int i = 0; i < s.q.answer_count; i++) {
        tile_t *t = &s.tiles[i];
        const ui_answer_style_t *st = ui_answer_style(i);
        if (i == correct) {
            set_tile_colors(t, st->bg, st->fg);
            show_badge(t, UI_SYMBOL_OK, UI_COLOR_TEXT, st->bg);
            ui_pop(t->tile);
        } else if (i == chosen) {
            set_tile_colors(t, lv_color_mix(st->bg, UI_COLOR_BG, LV_OPA_50), UI_COLOR_TEXT);
            show_badge(t, UI_SYMBOL_CLOSE, UI_COLOR_TEXT, UI_COLOR_BAD);
        } else {
            dim_tile(t);
        }
    }
}

void quiz_mark_opponent(int choice)
{
    if (choice >= 0 && choice < s.q.answer_count) {
        lv_obj_remove_flag(s.tiles[choice].other, LV_OBJ_FLAG_HIDDEN);
    }
}

void quiz_feedback(const char *text, lv_color_t bg, lv_color_t fg)
{
    lv_obj_add_flag(s.timer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s.feedback_label, text);
    lv_obj_set_style_text_color(s.feedback_label, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.feedback, bg, LV_PART_MAIN);
    lv_obj_remove_flag(s.feedback, LV_OBJ_FLAG_HIDDEN);
    ui_pop(s.feedback);
}

void quiz_stop(void)
{
    if (s.timer) {
        lv_anim_delete(s.timer, NULL);
    }
    memset(&s, 0, sizeof(s));
}

static void create_tile(lv_obj_t *parent, int i)
{
    tile_t *t = &s.tiles[i];
    const ui_answer_style_t *st = ui_answer_style(i);

    t->tile = lv_button_create(parent);
    lv_obj_remove_style_all(t->tile);
    lv_obj_set_style_radius(t->tile, UI_RADIUS_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t->tile, LV_OPA_COVER, LV_PART_MAIN);
    ui_make_pressable(t->tile);
    lv_obj_add_event_cb(t->tile, answer_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    t->icon = lv_image_create(t->tile);
    lv_image_set_src(t->icon, st->shape);
    lv_obj_set_style_image_recolor_opa(t->icon, LV_OPA_COVER, LV_PART_MAIN);

    t->label = ui_label(t->tile, UI_FONT_BODY, st->fg, "");
    lv_label_set_long_mode(t->label, LV_LABEL_LONG_MODE_WRAP);

    t->badge = lv_obj_create(t->tile);
    lv_obj_remove_style_all(t->badge);
    lv_obj_set_size(t->badge, BADGE_SIZE, BADGE_SIZE);
    lv_obj_set_style_radius(t->badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t->badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(t->badge, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_remove_flag(t->badge, LV_OBJ_FLAG_CLICKABLE);
    t->badge_label = ui_label(t->badge, UI_FONT_SMALL, UI_COLOR_TEXT, "");
    lv_obj_center(t->badge_label);

    /* Opponent's pick: a dot in their color, left of the badge. */
    t->other = lv_obj_create(t->tile);
    lv_obj_remove_style_all(t->other);
    lv_obj_set_size(t->other, OTHER_SIZE, OTHER_SIZE);
    lv_obj_set_style_radius(t->other, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t->other, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t->other, UI_COLOR_OPPONENT, LV_PART_MAIN);
    lv_obj_set_style_border_color(t->other, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_width(t->other, 3, LV_PART_MAIN);
    /* Dark edge, so it also stands out on the blue tile. */
    lv_obj_set_style_outline_color(t->other, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_outline_width(t->other, 2, LV_PART_MAIN);
    lv_obj_align(t->other, LV_ALIGN_TOP_RIGHT, -8 - BADGE_SIZE - 6, 8 + (BADGE_SIZE - OTHER_SIZE) / 2);
    lv_obj_remove_flag(t->other, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t->other, LV_OBJ_FLAG_HIDDEN);
}

void quiz_build(lv_obj_t *view, quiz_answer_cb_t on_answer, quiz_timeout_cb_t on_timeout)
{
    memset(&s, 0, sizeof(s));
    s.on_answer = on_answer;
    s.on_timeout = on_timeout;
    s.locked = true;

    s.timer = lv_arc_create(view);
    lv_obj_set_size(s.timer, TIMER_SIZE, TIMER_SIZE);
    lv_obj_align(s.timer, LV_ALIGN_TOP_MID, 0, 12);
    lv_arc_set_rotation(s.timer, 270);
    lv_arc_set_bg_angles(s.timer, 0, 360);
    lv_arc_set_range(s.timer, 0, TIMER_RANGE);
    lv_obj_remove_style(s.timer, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s.timer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s.timer, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s.timer, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s.timer, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s.timer, true, LV_PART_INDICATOR);
    s.timer_label = ui_label(s.timer, UI_FONT_BODY, UI_COLOR_TEXT, "");
    lv_obj_center(s.timer_label);

    s.feedback = ui_pill(view, &s.feedback_label, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s.feedback_label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s.feedback, 20, LV_PART_MAIN);
    lv_obj_align(s.feedback, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_add_flag(s.feedback, LV_OBJ_FLAG_HIDDEN);

    /* Question, centred vertically in its band. */
    lv_obj_t *band = lv_obj_create(view);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, AREA_W, QUESTION_H);
    lv_obj_set_pos(band, AREA_X, QUESTION_Y);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    s.question = ui_label(band, UI_FONT_QUESTION, UI_COLOR_TEXT, "");
    lv_label_set_long_mode(s.question, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s.question, AREA_W);
    lv_obj_set_style_text_align(s.question, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        create_tile(view, i);
    }
}
