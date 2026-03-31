#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Language = NULL;

void ui_Language_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Language != NULL)
    {
        return;
    }

    ui_Language = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Language, "语言", UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 582, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_button(panel, 50, 94, 486, 50, "简体中文", 20, UI_SCREEN_NONE, true);
    ui_create_button(panel, 50, 154, 486, 50, "English", 20, UI_SCREEN_NONE, false);
    ui_create_button(panel, 50, 214, 486, 50, "日本語", 20, UI_SCREEN_NONE, false);
}

void ui_Language_screen_destroy(void)
{
    if (ui_Language != NULL)
    {
        lv_obj_delete(ui_Language);
        ui_Language = NULL;
    }
}
