#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Weather = NULL;

void ui_Weather_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *metrics_box;

    if (ui_Weather != NULL)
    {
        return;
    }

    ui_Weather = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Weather, "天气", UI_SCREEN_HOME);

    ui_create_label(page.content,
                    "杭州 · 西湖区",
                    0,
                    52,
                    528,
                    25,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    "12℃",
                    0,
                    99,
                    528,
                    92,
                    74,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    "阴转小雨 · 空气良好",
                    0,
                    201,
                    528,
                    28,
                    22,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    metrics_box = ui_create_card(page.content, 24, 288, 480, 88, UI_SCREEN_NONE, false, 0);
    ui_create_label(metrics_box, "湿度 68%", 12, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    ui_create_label(metrics_box, "东北风 2 级", 164, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    ui_create_label(metrics_box, "体感 10℃", 316, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);

    ui_create_label(page.content,
                    "今天适合带上一首轻音乐与一本短篇小说。",
                    41,
                    420,
                    446,
                    34,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    true);
}

void ui_Weather_screen_destroy(void)
{
    if (ui_Weather != NULL)
    {
        lv_obj_delete(ui_Weather);
        ui_Weather = NULL;
    }
}
