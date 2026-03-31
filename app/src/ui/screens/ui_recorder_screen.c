#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Recorder = NULL;

void ui_Recorder_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *record_button;

    if (ui_Recorder != NULL)
    {
        return;
    }

    ui_Recorder = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Recorder, "录音", UI_SCREEN_HOME);

    ui_create_label(page.content,
                    "00:00",
                    0,
                    88,
                    582,
                    88,
                    90,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    "点击下方按钮开始录音",
                    0,
                    194,
                    582,
                    25,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    record_button = ui_create_card(page.content, 201, 246, 180, 180, UI_SCREEN_NONE, true, 90);
    ui_create_label(record_button,
                    "录音",
                    0,
                    64,
                    180,
                    44,
                    34,
                    LV_TEXT_ALIGN_CENTER,
                    true,
                    false);

    ui_create_button(page.content,
                     315,
                     507,
                     118,
                     48,
                     "录音记录",
                     20,
                     UI_SCREEN_RECORD_LIST,
                     true);
}

void ui_Recorder_screen_destroy(void)
{
    if (ui_Recorder != NULL)
    {
        lv_obj_delete(ui_Recorder);
        ui_Recorder = NULL;
    }
}
