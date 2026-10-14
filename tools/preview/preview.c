/*
 * Host preview: runs the UI code from components/ui against fake packs and a
 * fake touchscreen, and writes each scene to <name>.ppm (the Makefile turns
 * them into PNG). Time is simulated, so a 15 s timer takes no real time.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
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
    ui_show_home();
    run_ms(500);
    snap("01-home");

    tap_text("Algemene kennis");
    run_ms(400);
    snap("02-question");
    run_ms(6000);
    snap("03-question-6s");

    tap_text("Venus");
    run_ms(400);
    snap("04-reveal-wrong");
    run_ms(2000);
    snap("05-two-answers");

    tap_text("Ja");
    run_ms(400);
    snap("06-reveal-good");
    run_ms(2000);
    snap("07-three-answers");

    run_ms(16000);
    snap("08-timeout");
    run_ms(2000);
    tap_text("8");
    run_ms(2400);
    snap("09-result");

    ui_back();
    run_ms(500);
    ui_upload_progress("dieren", 3000, 7000);
    run_ms(100);
    snap("10-upload");
    ui_upload_finished("dieren", 0);
    run_ms(100);
    snap("11-upload-done");

    run_ms(3000);
    fake_pack_count = 0;
    ui_packs_changed();
    run_ms(300);
    snap("12-empty");
    return 0;
}
