#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Pomodoro = NULL;

void ui_Pomodoro_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *ring;

    if (ui_Pomodoro != NULL)
    {
        return;
    }

    ui_Pomodoro = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Pomodoro, ui_i18n_pick("番茄时间", "Pomodoro"), UI_SCREEN_TIME_MANAGE);

    ring = ui_create_card(page.content, 94, 64, 340, 340, UI_SCREEN_NONE, false, 170);
    ui_create_label(ring,
                    ui_i18n_pick("当前专注", "Focus Session"),
                    2,
                    76,
                    336,
                    20,
                    19,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(ring,
                    "25:00",
                    2,
                    118,
                    336,
                    95,
                    50,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(ring,
                    ui_i18n_pick("准备开始一段安静的阅读", "Ready for a calm reading session"),
                    50,
                    231,
                    240,
                    24,
                    15,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    ui_create_button(page.content, 122, 509, 118, 48, ui_i18n_pick("开始", "Start"), 20, UI_SCREEN_NONE, true);
    ui_create_button(page.content, 288, 509, 118, 48, ui_i18n_pick("重置", "Reset"), 20, UI_SCREEN_NONE, false);
}

void ui_Pomodoro_screen_destroy(void)
{
    if (ui_Pomodoro != NULL)
    {
        lv_obj_delete(ui_Pomodoro);
        ui_Pomodoro = NULL;
    }
}
