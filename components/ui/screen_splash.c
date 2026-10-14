/*
 * Splash view: the wordmark rises into place while the rest of the device
 * starts (storage, buttons, BLE).
 */
#include "ui_priv.h"

static void anim_rise(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(obj, v, LV_PART_MAIN);
    /* Fade in with text opacity: applied per glyph, so no extra draw layer. */
    lv_obj_set_style_text_opa(obj, (lv_opa_t)(255 - v * 255 / 40), LV_PART_MAIN);
}

void splash_show(void)
{
    ui_clean_screen();
    lv_obj_t *view = ui_view_create(false);

    lv_obj_t *logo = ui_label(view, UI_FONT_LOGO, UI_COLOR_BRAND, "pukk");
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -10);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, logo);
    lv_anim_set_exec_cb(&a, anim_rise);
    lv_anim_set_values(&a, 40, 0);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
