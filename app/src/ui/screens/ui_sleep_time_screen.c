#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Sleep_Time = NULL;

static lv_obj_t *create_divider(lv_obj_t *parent, int x, int y, int w)
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

void ui_Sleep_Time_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Sleep_Time != NULL)
    {
        return;
    }

    ui_Sleep_Time = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Sleep_Time, "助眠时间", UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    "夜间提醒将在设定时间后降低操作噪音与亮度。",
                    48,
                    92,
                    432,
                    29,
                    18,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);
    ui_create_label(panel,
                    "开始时间",
                    48,
                    141,
                    120,
                    42,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(panel,
                    "22:30",
                    378,
                    141,
                    102,
                    42,
                    24,
                    LV_TEXT_ALIGN_RIGHT,
                    false,
                    false);
    create_divider(panel, 44, 194, 440);

    ui_create_button(panel, 44, 209, 440, 50, "提前 10 分钟", 20, UI_SCREEN_NONE, false);
    ui_create_button(panel, 44, 269, 440, 50, "延后 10 分钟", 20, UI_SCREEN_NONE, false);
}

void ui_Sleep_Time_screen_destroy(void)
{
    if (ui_Sleep_Time != NULL)
    {
        lv_obj_delete(ui_Sleep_Time);
        ui_Sleep_Time = NULL;
    }
}
