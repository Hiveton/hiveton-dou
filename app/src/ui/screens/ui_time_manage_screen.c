#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Time_Manage = NULL;

static void create_time_menu_card(lv_obj_t *parent,
                                  int y,
                                  const char *title,
                                  const char *subtitle,
                                  ui_screen_id_t target)
{
    lv_obj_t *card = ui_create_card(parent, 24, y, 480, 112, target, false, 0);

    ui_create_label(card,
                    title,
                    24,
                    34,
                    432,
                    34,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    subtitle,
                    24,
                    79,
                    432,
                    21,
                    17,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
}

void ui_Time_Manage_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Time_Manage != NULL)
    {
        return;
    }

    ui_Time_Manage = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Time_Manage, "时间管理", UI_SCREEN_HOME);

    create_time_menu_card(page.content,
                          56,
                          "番茄时间",
                          "专注计时与休息切换",
                          UI_SCREEN_POMODORO);
    create_time_menu_card(page.content,
                          188,
                          "日期与时间",
                          "手动校准当前时间",
                          UI_SCREEN_DATETIME);
}

void ui_Time_Manage_screen_destroy(void)
{
    if (ui_Time_Manage != NULL)
    {
        lv_obj_delete(ui_Time_Manage);
        ui_Time_Manage = NULL;
    }
}
