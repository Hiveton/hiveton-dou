#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Brightness = NULL;

static lv_obj_t *create_brightness_track(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *track = lv_obj_create(parent);

    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(track, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(track, ui_px_w(w), ui_px_h(18));
    lv_obj_set_style_radius(track, ui_px_y(9), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(track, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(track, 2, 0);
    lv_obj_set_style_shadow_width(track, 0, 0);
    lv_obj_set_style_outline_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    return track;
}

static lv_obj_t *create_brightness_fill(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *fill = lv_obj_create(parent);

    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(fill, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(fill, ui_px_w(w), ui_px_h(10));
    lv_obj_set_style_radius(fill, ui_px_y(5), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_shadow_width(fill, 0, 0);
    lv_obj_set_style_outline_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    return fill;
}

void ui_Brightness_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Brightness != NULL)
    {
        return;
    }

    ui_Brightness = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Brightness, "屏幕亮度", UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    "当前亮度",
                    48,
                    92,
                    140,
                    42,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(panel,
                    "3 / 5",
                    376,
                    92,
                    98,
                    42,
                    24,
                    LV_TEXT_ALIGN_RIGHT,
                    false,
                    false);

    create_brightness_track(panel, 44, 162, 440);
    create_brightness_fill(panel, 48, 166, 260);
}

void ui_Brightness_screen_destroy(void)
{
    if (ui_Brightness != NULL)
    {
        lv_obj_delete(ui_Brightness);
        ui_Brightness = NULL;
    }
}
