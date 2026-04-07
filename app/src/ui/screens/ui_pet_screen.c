#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Pet = NULL;

void ui_Pet_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Pet != NULL)
    {
        return;
    }

    ui_Pet = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Pet, ui_i18n_pick("宠物管理", "Pet"), UI_SCREEN_HOME);

    ui_create_label(page.content,
                    ui_i18n_pick("宠物管理", "Pet Center"),
                    62,
                    166,
                    458,
                    50,
                    42,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    ui_i18n_pick("宠物状态、互动喂养与陪伴内容可继续在这一页扩展，当前先恢复主视觉与页面跳转。",
                                 "Pet status, feeding, and companion features can continue expanding here. This page currently restores the main visual and navigation flow."),
                    75,
                    234,
                    432,
                    60,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    true);
}

void ui_Pet_screen_destroy(void)
{
    if (ui_Pet != NULL)
    {
        lv_obj_delete(ui_Pet);
        ui_Pet = NULL;
    }
}
