/*
 * Battle views, driven by the engine's events (components/battle): lobby,
 * countdown, questions with both scores, and the final result. We are
 * yellow (brand), the opponent is blue.
 *
 * Buttons only tell the engine what we want; the view changes when the
 * engine reports back.
 */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "ui_priv.h"

#define SPINNER_SIZE    72
#define RING_SIZE       220
#define RING_RANGE      1000
#define COLUMN_W        200
#define COLUMN_H        150

typedef enum {
    BV_NONE,
    BV_LOBBY,
    BV_COUNTDOWN,
    BV_QUESTION,
    BV_RESULT,
} bview_t;

static struct {
    bview_t view;
    battle_role_t role;
    char opponent[24];
    /* lobby */
    lv_obj_t *lobby_title;
    lv_obj_t *lobby_spinner;
    lv_obj_t *lobby_ok;
    lv_obj_t *lobby_bar;
    lv_obj_t *lobby_main;
    lv_obj_t *lobby_hint;
    lv_obj_t *lobby_start;
    int lobby_status;
    /* countdown */
    lv_obj_t *ring;
    lv_obj_t *ring_label;
    int ring_seconds;
    /* question */
    lv_obj_t *progress;
    lv_obj_t *me_pill;
    lv_obj_t *me_label;
    lv_obj_t *them_pill;
    lv_obj_t *them_label;
    int my_choice;      /* tapped this question, or -1 */
} s;

/* "Pukk-AF8E" -> "AF8E", for tight spots. */
static const char *short_name(const char *name)
{
    return strncmp(name, "Pukk-", 5) == 0 && name[5] ? name + 5 : name;
}

static void leave_clicked_cb(lv_event_t *e)
{
    battle_leave();
}

static void start_clicked_cb(lv_event_t *e)
{
    battle_start();
}

static lv_obj_t *new_view(bview_t view)
{
    if (s.view == BV_QUESTION) {
        quiz_stop();
    }
    ui_clean_screen();
    s.view = view;
    return ui_view_create(true);
}

static lv_obj_t *centered_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t y)
{
    lv_obj_t *label = ui_label(parent, font, color, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, 480 - 2 * 24);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

/* --- lobby ----------------------------------------------------------------------- */

static void build_lobby(void)
{
    lv_obj_t *view = new_view(BV_LOBBY);
    s.lobby_status = -1;

    lv_obj_t *heading = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_BRAND, "Battle");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 28);
    s.lobby_title = centered_label(view, UI_FONT_BODY, UI_COLOR_TEXT_DIM, 94);
    lv_label_set_long_mode(s.lobby_title, LV_LABEL_LONG_MODE_DOTS);

    s.lobby_spinner = lv_spinner_create(view);
    lv_obj_set_size(s.lobby_spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_align(s.lobby_spinner, LV_ALIGN_TOP_MID, 0, 146);
    lv_spinner_set_anim_params(s.lobby_spinner, 1200, 240);
    lv_obj_set_style_arc_width(s.lobby_spinner, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s.lobby_spinner, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s.lobby_spinner, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s.lobby_spinner, UI_COLOR_OPPONENT, LV_PART_INDICATOR);

    s.lobby_ok = lv_obj_create(view);
    lv_obj_remove_style_all(s.lobby_ok);
    lv_obj_set_size(s.lobby_ok, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_align(s.lobby_ok, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_set_style_radius(s.lobby_ok, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.lobby_ok, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.lobby_ok, UI_COLOR_GOOD, LV_PART_MAIN);
    lv_obj_remove_flag(s.lobby_ok, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui_label(s.lobby_ok, UI_FONT_TITLE, UI_COLOR_ON_GOOD, UI_SYMBOL_OK));

    s.lobby_bar = lv_bar_create(view);
    lv_obj_remove_style_all(s.lobby_bar);
    lv_obj_set_size(s.lobby_bar, 320, 16);
    lv_obj_align(s.lobby_bar, LV_ALIGN_TOP_MID, 0, 174);
    lv_bar_set_range(s.lobby_bar, 0, 100);
    lv_obj_set_style_radius(s.lobby_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s.lobby_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s.lobby_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.lobby_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s.lobby_bar, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.lobby_bar, UI_COLOR_OPPONENT, LV_PART_INDICATOR);

    s.lobby_main = centered_label(view, UI_FONT_QUESTION, UI_COLOR_TEXT, 238);
    s.lobby_hint = centered_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 284);

    s.lobby_start = ui_button(view, "Start", UI_COLOR_BRAND, UI_COLOR_ON_BRAND, 260, start_clicked_cb, NULL);
    lv_obj_align(s.lobby_start, LV_ALIGN_BOTTOM_MID, 0, -100);

    lv_obj_t *leave = ui_button(view, "Stoppen", UI_COLOR_SURFACE, UI_COLOR_TEXT_DIM, 180, leave_clicked_cb,
                                NULL);
    lv_obj_set_height(leave, 48);
    lv_obj_align(leave, LV_ALIGN_BOTTOM_MID, 0, -28);
}

static void show_if(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_lobby(const battle_event_t *e)
{
    const bool host = e->role == BATTLE_ROLE_HOST;
    const char *name = e->opponent;
    char main[64];
    char hint[96];
    bool spinner = false, bar = false, start = false;

    switch (e->lobby.status) {
    case BATTLE_LOBBY_SEARCHING:
        spinner = true;
        if (host) {
            snprintf(main, sizeof(main), "Wachten op een tegenstander");
            snprintf(hint, sizeof(hint), "Kies op de andere Pukk “Meedoen”");
        } else {
            snprintf(main, sizeof(main), "Wachten op een uitnodiging");
            snprintf(hint, sizeof(hint), "Kies op de andere Pukk een pack\nen dan “Battle”. Dit is %s.",
                     ui_status()->device_name);
        }
        break;
    case BATTLE_LOBBY_CONNECTED:
        spinner = true;
        snprintf(main, sizeof(main), "%s gevonden!", name);
        snprintf(hint, sizeof(hint), "Even kijken of die dit pack heeft…");
        break;
    case BATTLE_LOBBY_TRANSFER:
        if (host) {
            bar = true;
            int percent = e->lobby.total ? (int)((uint64_t)e->lobby.sent * 100 / e->lobby.total) : 0;
            lv_bar_set_value(s.lobby_bar, percent, LV_ANIM_OFF);
            snprintf(main, sizeof(main), "Pack naar %s sturen", name);
            snprintf(hint, sizeof(hint), "%d%%", percent);
        } else {
            spinner = true;
            snprintf(main, sizeof(main), "Pack ontvangen");
            snprintf(hint, sizeof(hint), "van %s, even geduld…", name);
        }
        break;
    case BATTLE_LOBBY_READY:
        start = host;
        if (host) {
            snprintf(main, sizeof(main), "%s doet mee!", name);
            snprintf(hint, sizeof(hint), "Tik op Start als jullie klaar zijn");
        } else {
            snprintf(main, sizeof(main), "Je doet mee!");
            snprintf(hint, sizeof(hint), "Wachten tot %s start…", name);
        }
        break;
    default:
        return;
    }

    lv_label_set_text(s.lobby_title, e->title);
    show_if(s.lobby_title, e->title[0] != '\0');
    show_if(s.lobby_spinner, spinner);
    show_if(s.lobby_ok, e->lobby.status == BATTLE_LOBBY_READY);
    show_if(s.lobby_bar, bar);
    show_if(s.lobby_start, start);
    lv_label_set_text(s.lobby_main, main);
    lv_label_set_text(s.lobby_hint, hint);
    if (s.lobby_status != (int)e->lobby.status) {
        s.lobby_status = (int)e->lobby.status;
        if (e->lobby.status == BATTLE_LOBBY_READY) {
            ui_pop(s.lobby_ok);
        }
    }
}

/* --- countdown ------------------------------------------------------------------- */

static void ring_anim_cb(void *arc, int32_t value)
{
    lv_arc_set_value(arc, value % RING_RANGE == 0 && value ? RING_RANGE : value % RING_RANGE);
    int seconds = (int)((value + RING_RANGE - 1) / RING_RANGE);
    if (seconds != s.ring_seconds) {
        s.ring_seconds = seconds;
        if (seconds > 0) {
            lv_label_set_text_fmt(s.ring_label, "%d", seconds);
        } else {
            lv_label_set_text(s.ring_label, "Start!");
        }
        ui_pop(s.ring_label);
    }
}

static void build_countdown(const battle_event_t *e)
{
    lv_obj_t *view = new_view(BV_COUNTDOWN);

    lv_obj_t *me = ui_label(view, UI_FONT_TITLE, UI_COLOR_BRAND, "Jij");
    lv_obj_align(me, LV_ALIGN_TOP_MID, -110, 40);
    lv_obj_t *vs = ui_label(view, UI_FONT_BODY, UI_COLOR_TEXT_DIM, "tegen");
    lv_obj_align(vs, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_t *them = ui_label(view, UI_FONT_TITLE, UI_COLOR_OPPONENT, short_name(e->opponent));
    lv_obj_align(them, LV_ALIGN_TOP_MID, 110, 40);

    s.ring = lv_arc_create(view);
    lv_obj_set_size(s.ring, RING_SIZE, RING_SIZE);
    lv_obj_align(s.ring, LV_ALIGN_TOP_MID, 0, 120);
    lv_arc_set_rotation(s.ring, 270);
    lv_arc_set_bg_angles(s.ring, 0, 360);
    lv_arc_set_range(s.ring, 0, RING_RANGE);
    lv_obj_remove_style(s.ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s.ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s.ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s.ring, UI_COLOR_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s.ring, UI_COLOR_BRAND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s.ring, true, LV_PART_INDICATOR);
    s.ring_label = ui_label(s.ring, UI_FONT_DISPLAY, UI_COLOR_TEXT, "");
    lv_obj_center(s.ring_label);
    s.ring_seconds = -1;

    lv_obj_t *title = centered_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 380);
    lv_label_set_text(title, e->title);

    /* One ring per second, emptying. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.ring);
    lv_anim_set_exec_cb(&a, ring_anim_cb);
    lv_anim_set_values(&a, e->countdown.seconds * RING_RANGE, 0);
    lv_anim_set_duration(&a, e->countdown.seconds * 1000u);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

/* --- questions -------------------------------------------------------------------- */

static void set_scores(const battle_event_t *e)
{
    lv_label_set_text_fmt(s.me_label, "Jij  %lu", (unsigned long)e->score[BATTLE_ME]);
    lv_label_set_text_fmt(s.them_label, "%s  %lu", short_name(s.opponent),
                          (unsigned long)e->score[BATTLE_THEM]);
}

static void on_answer(int choice)
{
    s.my_choice = choice;
    quiz_mark_choice(choice);
    battle_answer((uint8_t)choice);
}

static void on_timeout(void)
{
    /* Wait for the result: it decides what we show. */
}

static void build_question(void)
{
    lv_obj_t *view = new_view(BV_QUESTION);

    s.progress = lv_bar_create(view);
    lv_obj_remove_style_all(s.progress);
    lv_obj_set_size(s.progress, 480 - 2 * UI_SCREEN_PAD, 6);
    lv_obj_align(s.progress, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(s.progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s.progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s.progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s.progress, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.progress, UI_COLOR_TEXT_DIM, LV_PART_INDICATOR);

    s.me_pill = ui_pill(view, &s.me_label, UI_COLOR_BRAND);
    lv_obj_align(s.me_pill, LV_ALIGN_TOP_LEFT, UI_SCREEN_PAD, 26);
    s.them_pill = ui_pill(view, &s.them_label, UI_COLOR_OPPONENT);
    lv_obj_align(s.them_pill, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD, 26);
    quiz_build(view, on_answer, on_timeout);
}

static void show_question(const battle_event_t *e)
{
    if (s.view != BV_QUESTION) {
        build_question();
    }
    lv_bar_set_range(s.progress, 0, e->question.count);
    lv_bar_set_value(s.progress, e->question.index + 1, LV_ANIM_OFF);
    set_scores(e);
    s.my_choice = -1;
    quiz_show(e->question.q);
}

static void show_round(const battle_event_t *e)
{
    const int correct = e->round.correct;
    int mine = s.my_choice;
    if (mine < 0 && e->round.choice[BATTLE_ME] < PACK_MAX_ANSWERS) {
        mine = e->round.choice[BATTLE_ME];
    }
    quiz_reveal(correct, mine != correct ? mine : -1);
    if (e->round.choice[BATTLE_THEM] < PACK_MAX_ANSWERS) {
        quiz_mark_opponent(e->round.choice[BATTLE_THEM]);
    }

    char text[32];
    if (e->round.winner == BATTLE_ME) {
        snprintf(text, sizeof(text), UI_SYMBOL_OK " +%u", e->round.points);
        quiz_feedback(text, UI_COLOR_GOOD, UI_COLOR_ON_GOOD);
    } else if (mine >= 0 && mine != correct) {
        quiz_feedback(UI_SYMBOL_CLOSE " Fout", UI_COLOR_BAD, UI_COLOR_TEXT);
    } else if (e->round.winner == BATTLE_THEM) {
        quiz_feedback("Te laat!", UI_COLOR_WARN, UI_COLOR_ON_WARN);
    } else {
        quiz_feedback("Tijd is om!", UI_COLOR_WARN, UI_COLOR_ON_WARN);
    }

    set_scores(e);
    if (e->round.winner == BATTLE_ME) {
        ui_pop(s.me_pill);
    } else if (e->round.winner == BATTLE_THEM) {
        ui_pop(s.them_pill);
    }
}

/* --- result ------------------------------------------------------------------------ */

static void add_column(lv_obj_t *view, int32_t x, const char *name, lv_color_t color, uint32_t score,
                       bool winner)
{
    lv_obj_t *card = lv_obj_create(view);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, ui_style_card(), LV_PART_MAIN);
    lv_obj_set_size(card, COLUMN_W, COLUMN_H);
    lv_obj_align(card, LV_ALIGN_TOP_MID, x, 136);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (winner) {
        lv_obj_set_style_border_color(card, color, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 4, LV_PART_MAIN);
    }

    lv_obj_t *label = ui_label(card, UI_FONT_BODY, color, name);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 6);
    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)score);
    lv_obj_t *points = ui_label(card, UI_FONT_DISPLAY, UI_COLOR_TEXT, text);
    lv_obj_align(points, LV_ALIGN_BOTTOM_MID, 0, -8);
    if (winner) {
        lv_obj_t *star = ui_label(view, UI_FONT_TITLE, color, UI_SYMBOL_STAR);
        lv_obj_align_to(star, card, LV_ALIGN_OUT_TOP_MID, 0, -2);
        ui_pop(star);
        ui_pop(card);
    }
}

static void build_result(const battle_event_t *e)
{
    lv_obj_t *view = new_view(BV_RESULT);
    const uint32_t mine = e->score[BATTLE_ME];
    const uint32_t theirs = e->score[BATTLE_THEM];

    lv_obj_t *heading;
    if (mine > theirs) {
        heading = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_BRAND, "Jij wint!");
    } else if (theirs > mine) {
        char text[40];
        snprintf(text, sizeof(text), "%s wint!", short_name(e->opponent));
        heading = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_OPPONENT, text);
    } else {
        heading = ui_label(view, UI_FONT_DISPLAY, UI_COLOR_TEXT, "Gelijkspel!");
    }
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 32);

    const int32_t offset = COLUMN_W / 2 + 8;
    add_column(view, -offset, "Jij", UI_COLOR_BRAND, mine, mine > theirs);
    add_column(view, offset, short_name(e->opponent), UI_COLOR_OPPONENT, theirs, theirs > mine);

    lv_obj_t *title = centered_label(view, UI_FONT_SMALL, UI_COLOR_TEXT_DIM, 306);
    lv_label_set_text(title, e->title);

    lv_obj_t *back = ui_button(view, "Terug", UI_COLOR_BRAND, UI_COLOR_ON_BRAND, 220, leave_clicked_cb, NULL);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -40);
}

/* --- events ------------------------------------------------------------------------- */

void battle_view_event(const battle_event_t *e)
{
    s.role = e->role;
    snprintf(s.opponent, sizeof(s.opponent), "%s", e->opponent);

    switch (e->type) {
    case BATTLE_EVENT_LOBBY:
        if (s.view != BV_LOBBY) {
            build_lobby();
        }
        update_lobby(e);
        break;
    case BATTLE_EVENT_COUNTDOWN:
        build_countdown(e);
        break;
    case BATTLE_EVENT_QUESTION:
        show_question(e);
        break;
    case BATTLE_EVENT_ROUND:
        if (s.view == BV_QUESTION) {
            show_round(e);
        }
        break;
    case BATTLE_EVENT_FINISHED:
        build_result(e);
        break;
    default:
        break;
    }
}

void battle_view_stop(void)
{
    if (s.view == BV_QUESTION) {
        quiz_stop();
    }
    memset(&s, 0, sizeof(s));
}
