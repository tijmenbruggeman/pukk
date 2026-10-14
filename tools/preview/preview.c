/*
 * Host preview: runs the UI code from components/ui against fake packs and a
 * fake touchscreen, and writes each scene to <name>.ppm (the Makefile turns
 * them into PNG). Time is simulated, so a 15 s timer takes no real time.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "packs.h"
#include "ui.h"
#include "ui_priv.h"

#define W 480
#define H 480

extern size_t fake_pack_count;

static uint16_t s_frame[W * H];
static lv_display_t *s_disp;
static uint32_t s_ms;
static lv_point_t s_touch;
static bool s_pressed;

/* --- stand-ins for lvgl_port.c ------------------------------------------- */

bool ui_lock(int timeout_ms) { return true; }
void ui_unlock(void) {}
size_t ui_task_stack_free(void) { return 0; }

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    lv_display_flush_ready(disp); /* direct mode: s_frame is the frame */
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point = s_touch;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* --- helpers --------------------------------------------------------------- */

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void tap_at(int32_t x, int32_t y)
{
    s_touch.x = x;
    s_touch.y = y;
    s_pressed = true;
    run_ms(80);
    s_pressed = false;
    run_ms(80);
}

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) {
        return obj;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *found = find_label(lv_obj_get_child(obj, (int32_t)i), text);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* Tap the centre of the label showing `text`. */
static void tap_text(const char *text)
{
    lv_obj_t *label = find_label(lv_screen_active(), text);
    if (!label) {
        printf("preview: no label \"%s\" on screen\n", text);
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(label, &a);
    tap_at((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

static void snap(const char *name)
{
    lv_refr_now(s_disp);
    char path[64];
    snprintf(path, sizeof(path), "%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_frame[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
            (uint8_t)((c & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("preview: %s\n", name);
}

/* --- battle events, as the engine would send them ------------------------------ */

static pack_question_t s_q;

static battle_event_t battle_ev(battle_event_type_t type, battle_role_t role, const char *opponent,
                                const char *title, uint32_t mine, uint32_t theirs)
{
    battle_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.role = role;
    e.opponent = opponent;
    e.title = title;
    e.score[BATTLE_ME] = mine;
    e.score[BATTLE_THEM] = theirs;
    return e;
}

static void lobby(battle_role_t role, battle_lobby_t status, const char *opponent, const char *title,
                  uint32_t sent, uint32_t total)
{
    battle_event_t e = battle_ev(BATTLE_EVENT_LOBBY, role, opponent, title, 0, 0);
    e.lobby.status = status;
    e.lobby.sent = sent;
    e.lobby.total = total;
    ui_battle_event(&e);
}

static void battle_question(uint16_t index, uint32_t mine, uint32_t theirs)
{
    packs_read_question("algemeen", index, &s_q);
    battle_event_t e = battle_ev(BATTLE_EVENT_QUESTION, BATTLE_ROLE_HOST, "Pukk-AF8E", "Algemene kennis",
                                 mine, theirs);
    e.question.q = &s_q;
    e.question.index = index;
    e.question.count = 4;
    ui_battle_event(&e);
}

static void battle_round(uint8_t winner, uint8_t mine_choice, uint8_t their_choice, uint16_t points,
                         uint32_t mine, uint32_t theirs)
{
    battle_event_t e = battle_ev(BATTLE_EVENT_ROUND, BATTLE_ROLE_HOST, "Pukk-AF8E", "Algemene kennis",
                                 mine, theirs);
    e.round.correct = s_q.correct;
    e.round.winner = winner;
    e.round.choice[BATTLE_ME] = mine_choice;
    e.round.choice[BATTLE_THEM] = their_choice;
    e.round.points = points;
    ui_battle_event(&e);
}

static void battle_scenes(void)
{
    fake_pack_count = 3;
    ui_packs_changed();
    run_ms(300);
    tap_text("Algemene kennis");
    run_ms(400);
    tap_text("Battle");

    lobby(BATTLE_ROLE_HOST, BATTLE_LOBBY_SEARCHING, "", "Algemene kennis", 0, 0);
    run_ms(500);
    snap("20-lobby-host-searching");
    lobby(BATTLE_ROLE_HOST, BATTLE_LOBBY_CONNECTED, "Pukk-AF8E", "Algemene kennis", 0, 0);
    run_ms(300);
    snap("21-lobby-host-connected");
    lobby(BATTLE_ROLE_HOST, BATTLE_LOBBY_TRANSFER, "Pukk-AF8E", "Algemene kennis", 2900, 7000);
    run_ms(300);
    snap("22-lobby-host-transfer");
    lobby(BATTLE_ROLE_HOST, BATTLE_LOBBY_READY, "Pukk-AF8E", "Algemene kennis", 0, 0);
    run_ms(500);
    snap("23-lobby-host-ready");
    tap_text("Start");

    battle_event_t e = battle_ev(BATTLE_EVENT_COUNTDOWN, BATTLE_ROLE_HOST, "Pukk-AF8E", "Algemene kennis",
                                 0, 0);
    e.countdown.seconds = BATTLE_COUNTDOWN_S;
    ui_battle_event(&e);
    run_ms(1400);
    snap("24-countdown");

    run_ms(1700);
    battle_question(0, 0, 0);
    run_ms(2500);
    snap("25-battle-question");
    tap_text("Venus");
    run_ms(300);
    snap("26-battle-answered");
    run_ms(900);
    battle_round(BATTLE_THEM, 0, 1, 870, 0, 870);
    run_ms(450);
    snap("27-battle-round-wrong");

    run_ms(2000);
    battle_question(1, 0, 870);
    run_ms(1200);
    tap_text("Ja");
    run_ms(200);
    battle_round(BATTLE_ME, 0, BATTLE_LATE, 920, 920, 870);
    run_ms(450);
    snap("28-battle-round-won");

    run_ms(2000);
    battle_question(2, 920, 870);
    run_ms(3000);
    battle_round(BATTLE_THEM, BATTLE_LATE, 1, 810, 920, 1680);
    run_ms(450);
    snap("29-battle-round-late");

    e = battle_ev(BATTLE_EVENT_FINISHED, BATTLE_ROLE_HOST, "Pukk-AF8E", "Algemene kennis", 920, 1680);
    ui_battle_event(&e);
    run_ms(600);
    snap("30-battle-result");
    tap_text("Terug");

    e = battle_ev(BATTLE_EVENT_STOPPED, BATTLE_ROLE_HOST, "Pukk-AF8E", "Algemene kennis", 0, 0);
    e.stopped.reason = BATTLE_STOP_LEFT;
    ui_battle_event(&e);
    run_ms(500);
    tap_text("Meedoen");
    lobby(BATTLE_ROLE_GUEST, BATTLE_LOBBY_SEARCHING, "", "", 0, 0);
    run_ms(500);
    snap("31-lobby-guest-searching");
    lobby(BATTLE_ROLE_GUEST, BATTLE_LOBBY_TRANSFER, "Pukk-EA66", "dieren", 0, 0);
    ui_upload_progress("dieren", 4096, 7000);
    run_ms(300);
    snap("32-lobby-guest-transfer");
    ui_upload_finished("dieren", 0);
    run_ms(2800);
    lobby(BATTLE_ROLE_GUEST, BATTLE_LOBBY_READY, "Pukk-EA66", "Dieren in de Ardèche", 0, 0);
    run_ms(500);
    snap("33-lobby-guest-ready");

    e = battle_ev(BATTLE_EVENT_STOPPED, BATTLE_ROLE_GUEST, "Pukk-EA66", "Dieren in de Ardèche", 0, 0);
    e.stopped.reason = BATTLE_STOP_OPPONENT;
    ui_battle_event(&e);
    run_ms(400);
    snap("34-battle-stopped");
}

/* --- scenes ---------------------------------------------------------------- */

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(W, H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_frame, NULL, sizeof(s_frame), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_cb);

    ui_set_device_name("Pukk-EA66");
    ui_set_battery(82, false);
    ui_show_splash();
    run_ms(300);
    snap("00-splash-rising");
    run_ms(700);
    snap("00-splash");
    ui_show_home();
    run_ms(1000);
    snap("01-home");

    tap_text("Algemene kennis");
    run_ms(400);
    snap("02-pack");
    tap_text("Spelen");
    run_ms(400);
    snap("03-question");
    run_ms(9000);
    snap("04-question-9s");

    tap_text("Venus");
    run_ms(450);
    snap("05-reveal-wrong");
    run_ms(2000);
    snap("06-two-answers");

    tap_text("Ja");
    run_ms(450);
    snap("07-reveal-good");
    run_ms(2000);
    snap("08-three-answers");

    run_ms(16000);
    snap("09-timeout");
    run_ms(2000);
    tap_text("8");
    run_ms(2600);
    snap("10-result");

    tap_text("Terug");
    run_ms(500);
    ui_upload_progress("dieren", 3000, 7000);
    run_ms(300);
    snap("11-upload");
    ui_upload_finished("dieren", 0);
    run_ms(300);
    snap("12-upload-done");

    run_ms(3000);
    ui_set_ble_connected(true);
    ui_set_battery(9, false);
    run_ms(100);
    snap("13-home-ble-low-battery");

    fake_pack_count = 0;
    ui_set_battery(45, true);
    ui_packs_changed();
    run_ms(300);
    snap("14-empty-charging");

    ui_set_battery(82, false);
    battle_scenes();
    return 0;
}
