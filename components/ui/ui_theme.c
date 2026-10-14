#include <string.h>
#include "ui_theme.h"

/* Answer colors with their shapes. Text on amber and green is dark for contrast. */
static const ui_answer_style_t s_answers[4] = {
    { LV_COLOR_MAKE(0xFF, 0x5A, 0x5F), LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), &pukk_shape_triangle },
    { LV_COLOR_MAKE(0x3D, 0x8B, 0xFF), LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), &pukk_shape_diamond },
    { LV_COLOR_MAKE(0xFF, 0xB0, 0x20), LV_COLOR_MAKE(0x3A, 0x25, 0x00), &pukk_shape_circle },
    { LV_COLOR_MAKE(0x2B, 0xC4, 0x8A), LV_COLOR_MAKE(0x06, 0x30, 0x1F), &pukk_shape_square },
};

static lv_style_t s_card;
static lv_style_t s_pill;
static lv_style_t s_pressed;
static bool s_ready;

void ui_theme_init(void)
{
    if (s_ready) {
        return;
    }
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, UI_COLOR_SURFACE);
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, UI_RADIUS_CARD);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_shadow_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, 12);

    lv_style_init(&s_pill);
    lv_style_set_bg_color(&s_pill, UI_COLOR_SURFACE);
    lv_style_set_bg_opa(&s_pill, LV_OPA_COVER);
    lv_style_set_radius(&s_pill, LV_RADIUS_CIRCLE);
    lv_style_set_pad_hor(&s_pill, 14);
    lv_style_set_pad_ver(&s_pill, 4);

    /* Draw 4 px smaller on every side: no layout change, no extra layer. */
    lv_style_init(&s_pressed);
    lv_style_set_transform_width(&s_pressed, -4);
    lv_style_set_transform_height(&s_pressed, -4);

    s_ready = true;
}

const ui_answer_style_t *ui_answer_style(int index)
{
    return &s_answers[index & 3];
}

lv_color_t ui_pack_color(const char *name, lv_color_t *fg)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    for (; *name; name++) {
        h = (h ^ (uint8_t)*name) * 16777619u;
    }
    const ui_answer_style_t *s = &s_answers[h % 4];
    *fg = s->fg;
    return s->bg;
}

lv_style_t *ui_style_card(void)
{
    return &s_card;
}

lv_style_t *ui_style_pill(void)
{
    return &s_pill;
}

static void anim_translate_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(obj, v, LV_PART_MAIN);
}

lv_obj_t *ui_view_create(bool animate)
{
    lv_obj_t *view = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(view);
    lv_obj_set_size(view, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(view, LV_OBJ_FLAG_SCROLLABLE);
    if (animate) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, view);
        lv_anim_set_exec_cb(&a, anim_translate_y);
        lv_anim_set_values(&a, 24, 0);
        lv_anim_set_duration(&a, 220);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    return view;
}

void ui_make_pressable(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_pressed, LV_STATE_PRESSED);
}

static void anim_grow(void *obj, int32_t v)
{
    lv_obj_set_style_transform_width(obj, v, LV_PART_MAIN);
    lv_obj_set_style_transform_height(obj, v, LV_PART_MAIN);
}

void ui_pop(lv_obj_t *obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_grow);
    lv_anim_set_values(&a, 0, 8);
    lv_anim_set_duration(&a, 140);
    lv_anim_set_reverse_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
