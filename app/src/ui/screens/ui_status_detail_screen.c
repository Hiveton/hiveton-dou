#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Status_Detail = NULL;

static ui_screen_id_t s_status_detail_return_target = UI_SCREEN_HOME;

void ui_Status_Detail_screen_set_return_target(ui_screen_id_t target)
{
    if (target == UI_SCREEN_NONE || target == UI_SCREEN_STATUS_DETAIL)
    {
        target = UI_SCREEN_HOME;
    }

    s_status_detail_return_target = target;
}

void ui_Status_Detail_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Status_Detail != NULL)
    {
        return;
    }

    ui_Status_Detail = ui_create_screen_base();
    ui_build_standard_screen_ex(&page,
                                ui_Status_Detail,
                                "快捷状态",
                                s_status_detail_return_target,
                                false);
    ui_build_status_detail_content(ui_Status_Detail, page.content);
}

void ui_Status_Detail_screen_destroy(void)
{
    if (ui_Status_Detail != NULL)
    {
        lv_obj_delete(ui_Status_Detail);
        ui_Status_Detail = NULL;
    }
}
