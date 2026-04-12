#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Datetime = NULL;

static void create_datetime_row(lv_obj_t *parent,
                                int y,
                                const char *name,
                                const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_pos(row, ui_px_x(44), ui_px_y(y));
    lv_obj_set_size(row, ui_px_w(440), ui_px_h(84));

    ui_create_label(row, name, 8, 23, 18, 25, 22, LV_TEXT_ALIGN_LEFT, false, false);
    ui_create_button(row, 140, 23, 42, 42, "-", 26, UI_SCREEN_NONE, false);
    ui_create_label(row, value, 198, 23, 56, 39, 32, LV_TEXT_ALIGN_CENTER, false, false);
    ui_create_button(row, 300, 23, 42, 42, "+", 26, UI_SCREEN_NONE, false);
}

void ui_Datetime_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Datetime != NULL)
    {
        return;
    }

    ui_Datetime = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Datetime, ui_i18n_pick("日期与时间", "Date & Time"), UI_SCREEN_TIME_MANAGE);

    ui_create_label(page.content,
                    ui_i18n_pick("当前设备时间", "Current Device Time"),
                    44,
                    62,
                    440,
                    20,
                    18,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(page.content,
                    "2026 / 01 / 14 15 : 30",
                    44,
                    102,
                    440,
                    39,
                    32,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    create_datetime_row(page.content, 169, ui_i18n_pick("年", "Year"), "2026");
    create_datetime_row(page.content, 253, ui_i18n_pick("月", "Month"), "01");
    create_datetime_row(page.content, 337, ui_i18n_pick("日", "Day"), "14");
    create_datetime_row(page.content, 421, ui_i18n_pick("时", "Hour"), "15");
    create_datetime_row(page.content, 505, ui_i18n_pick("分", "Minute"), "30");
}

void ui_Datetime_screen_destroy(void)
{
    if (ui_Datetime != NULL)
    {
        lv_obj_delete(ui_Datetime);
        ui_Datetime = NULL;
    }
}
