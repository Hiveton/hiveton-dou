#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Weather_Toggle = NULL;

static lv_obj_t *create_toggle_divider(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *line = lv_obj_create(parent);

    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(line, ui_px_w(w), 1);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_shadow_width(line, 0, 0);
    lv_obj_set_style_outline_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xD9D9D9), 0);
    return line;
}

void ui_Weather_Toggle_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Weather_Toggle != NULL)
    {
        return;
    }

    ui_Weather_Toggle = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Weather_Toggle, "天气开关", UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 582, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    "首页显示天气",
                    48,
                    92,
                    180,
                    42,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_button(panel, 398, 92, 88, 40, "开启", 18, UI_SCREEN_NONE, true);
    create_toggle_divider(panel, 48, 146, 486);
    ui_create_label(panel,
                    "关闭后首页不显示天气入口，但天气功能仍可从设置页进入。",
                    48,
                    160,
                    486,
                    58,
                    18,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);
}

void ui_Weather_Toggle_screen_destroy(void)
{
    if (ui_Weather_Toggle != NULL)
    {
        lv_obj_delete(ui_Weather_Toggle);
        ui_Weather_Toggle = NULL;
    }
}
