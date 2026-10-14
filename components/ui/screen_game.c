/*
 * Game view: the questions of one pack in order, each with its own timer,
 * then the result. Questions are read from flash one at a time.
 *
 * Layout (480 x 480):
 *   y  12- 76  header: progress pill, timer ring (or feedback), score pill
 *   y  84-220  question, centred vertically
 *   y 232-468  answer tiles: 2 x 2 grid (4), rows (3) or columns (2)
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "packs.h"
#include "ui_priv.h"

static const char *TAG = "game";

#define TIMER_RANGE     1000
#define TIMER_SIZE      64
#define REVEAL_MS       2200
#define TIMED_OUT       (-1)

#define QUESTION_Y      84
#define QUESTION_H      136
#define AREA_X          UI_SCREEN_PAD
#define AREA_Y          232
#define AREA_W          (480 - 2 * UI_SCREEN_PAD)
#define AREA_H          (480 - AREA_Y - 12)
#define GAP             12
#define BADGE_SIZE      34

typedef struct {
    lv_obj_t *tile;
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_obj_t *badge;
    lv_obj_t *badge_label;
} tile_t;

static struct {
    bool active;
    char pack[PACKS_NAME_MAX + 1];
    char title[PACK_TITLE_MAX + 1];
    uint16_t count;
    uint16_t index;
    uint16_t score;
    bool answered;
    int timer_band;
    int timer_seconds;
    pack_question_t q;
    lv_obj_t *progress;
    lv_obj_t *score_label;
    lv_obj_t *timer;
    lv_obj_t *timer_label;
    lv_obj_t *feedback;
    lv_obj_t *feedback_label;
    lv_obj_t *question;
    tile_t tiles[PACK_MAX_ANSWERS];
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

    lv_obj_t *back = lv_button_create(view);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, ui_style_pill(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, UI_COLOR_BRAND, LV_PART_MAIN);
    lv_obj_set_size(back, 220, 64);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -40);
    ui_make_pressable(back);
    lv_obj_add_event_cb(back, back_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_label(back, UI_FONT_BODY, UI_COLOR_ON_BRAND, "Terug"));
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

static void reveal(int chosen)
{
    g.answered = true;
    lv_anim_delete(g.timer, NULL);

    for (int i = 0; i < g.q.answer_count; i++) {
        tile_t *t = &g.tiles[i];
        const ui_answer_style_t *s = ui_answer_style(i);
        if (i == g.q.correct) {
            show_badge(t, UI_SYMBOL_OK, UI_COLOR_TEXT, s->bg);
            ui_pop(t->tile);
        } else if (i == chosen) {
            set_tile_colors(t, lv_color_mix(s->bg, UI_COLOR_BG, LV_OPA_50), UI_COLOR_TEXT);
            show_badge(t, UI_SYMBOL_CLOSE, UI_COLOR_TEXT, UI_COLOR_BAD);
        } else {
            set_tile_colors(t, UI_COLOR_SURFACE, UI_COLOR_TEXT_DIM);
        }
    }

    const char *text;
    lv_color_t bg, fg;
    if (chosen == g.q.correct) {
        g.score++;
        text = UI_SYMBOL_OK " Goed!";
        bg = UI_COLOR_GOOD;
        fg = lv_color_hex(0x06301F);
    } else if (chosen == TIMED_OUT) {
        text = "Tijd is om!";
        bg = UI_COLOR_WARN;
        fg = lv_color_hex(0x3A2500);
    } else {
        text = UI_SYMBOL_CLOSE " Fout";
        bg = UI_COLOR_BAD;
        fg = UI_COLOR_TEXT;
    }
    update_header();
    lv_obj_add_flag(g.timer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g.feedback_label, text);
    lv_obj_set_style_text_color(g.feedback_label, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.feedback, bg, LV_PART_MAIN);
    lv_obj_remove_flag(g.feedback, LV_OBJ_FLAG_HIDDEN);
    ui_pop(g.feedback);

    g.next_timer = lv_timer_create(next_timer_cb, REVEAL_MS, NULL);
    lv_timer_set_repeat_count(g.next_timer, 1);
}

static void answer_clicked_cb(lv_event_t *e)
{
    if (!g.answered) {
        reveal((int)(intptr_t)lv_event_get_user_data(e));
    }
}

static void timer_anim_cb(void *arc, int32_t value)
{
    lv_arc_set_value(arc, value);

    int seconds = (int)((value * g.q.time_s + TIMER_RANGE - 1) / TIMER_RANGE);
    if (seconds != g.timer_seconds) {
        g.timer_seconds = seconds;
        lv_label_set_text_fmt(g.timer_label, "%d", seconds);
    }
    /* Ring turns orange, then red, as time runs out. */
    int band = value > TIMER_RANGE / 2 ? 0 : value > TIMER_RANGE / 5 ? 1 : 2;
    if (band != g.timer_band) {
        g.timer_band = band;
        lv_color_t c = band == 0 ? UI_COLOR_BRAND : band == 1 ? UI_COLOR_WARN : UI_COLOR_BAD;
        lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    }
}

static void timer_done_cb(lv_anim_t *a)
{
    if (!g.answered) {
        reveal(TIMED_OUT);
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
    lv_obj_remove_flag(g.timer, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        tile_t *t = &g.tiles[i];
        if (i >= g.q.answer_count) {
            lv_obj_add_flag(t->tile, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ui_answer_style_t *s = ui_answer_style(i);
        lv_obj_remove_flag(t->tile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t->badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(t->label, g.q.answers[i]);
        set_tile_colors(t, s->bg, s->fg);
        layout_tile(t, i, g.q.answer_count);
    }

    lv_anim_delete(g.timer, NULL);
    g.timer_band = -1;
    g.timer_seconds = -1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g.timer);
    lv_anim_set_exec_cb(&a, timer_anim_cb);
    lv_anim_set_values(&a, TIMER_RANGE, 0);
    lv_anim_set_duration(&a, (uint32_t)g.q.time_s * 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, timer_done_cb);
    lv_anim_start(&a);
}

static lv_obj_t *create_pill(lv_obj_t *parent, lv_obj_t **label, lv_color_t color)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_add_style(pill, ui_style_pill(), LV_PART_MAIN);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    *label = ui_label(pill, UI_FONT_SMALL, color, "");
    return pill;
}

static void create_tile(lv_obj_t *parent, int i)
{
    tile_t *t = &g.tiles[i];
    const ui_answer_style_t *s = ui_answer_style(i);

    t->tile = lv_button_create(parent);
    lv_obj_remove_style_all(t->tile);
    lv_obj_set_style_radius(t->tile, UI_RADIUS_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t->tile, LV_OPA_COVER, LV_PART_MAIN);
    ui_make_pressable(t->tile);
    lv_obj_add_event_cb(t->tile, answer_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    t->icon = lv_image_create(t->tile);
    lv_image_set_src(t->icon, s->shape);
    lv_obj_set_style_image_recolor_opa(t->icon, LV_OPA_COVER, LV_PART_MAIN);

    t->label = ui_label(t->tile, UI_FONT_BODY, s->fg, "");
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
}

static void build_view(void)
{
    ui_clean_screen();
    lv_obj_t *view = ui_view_create(true);

    lv_obj_t *progress = create_pill(view, &g.progress, UI_COLOR_TEXT);
    lv_obj_align(progress, LV_ALIGN_TOP_LEFT, UI_SCREEN_PAD, 26);
    lv_obj_t *score = create_pill(view, &g.score_label, UI_COLOR_BRAND);
    lv_obj_align(score, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD, 26);

    g.timer = lv_arc_create(view);
    lv_obj_set_size(g.timer, TIMER_SIZE, TIMER_SIZE);
    lv_obj_align(g.timer, LV_ALIGN_TOP_MID, 0, 12);
    lv_arc_set_rotation(g.timer, 270);
    lv_arc_set_bg_angles(g.timer, 0, 360);
    lv_arc_set_range(g.timer, 0, TIMER_RANGE);
    lv_obj_remove_style(g.timer, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g.timer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g.timer, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g.timer, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g.timer, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g.timer, true, LV_PART_INDICATOR);
    g.timer_label = ui_label(g.timer, UI_FONT_BODY, UI_COLOR_TEXT, "");
    lv_obj_center(g.timer_label);

    g.feedback = create_pill(view, &g.feedback_label, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(g.feedback_label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(g.feedback, 20, LV_PART_MAIN);
    lv_obj_align(g.feedback, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_add_flag(g.feedback, LV_OBJ_FLAG_HIDDEN);

    /* Question, centred vertically in its band. */
    lv_obj_t *band = lv_obj_create(view);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, AREA_W, QUESTION_H);
    lv_obj_set_pos(band, AREA_X, QUESTION_Y);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    g.question = ui_label(band, UI_FONT_QUESTION, UI_COLOR_TEXT, "");
    lv_label_set_long_mode(g.question, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(g.question, AREA_W);
    lv_obj_set_style_text_align(g.question, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    for (int i = 0; i < PACK_MAX_ANSWERS; i++) {
        create_tile(view, i);
    }
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
    lv_anim_delete(g.timer, NULL);
    g.active = false;
}
