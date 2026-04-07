#include "ui.h"
#include "ui_i18n.h"
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
    ui_build_standard_screen(&page, ui_Recorder, ui_i18n_pick("录音", "Recorder"), UI_SCREEN_HOME);

    ui_create_label(page.content,
                    "00:00",
                    0,
                    88,
                    528,
                    88,
                    90,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    ui_i18n_pick("点击下方按钮开始录音", "Tap the button below to start recording"),
                    0,
                    194,
                    528,
                    25,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    record_button = ui_create_card(page.content, 174, 246, 180, 180, UI_SCREEN_NONE, true, 90);
    ui_create_label(record_button,
                    ui_i18n_pick("录音", "Record"),
                    0,
                    64,
                    180,
                    44,
                    34,
                    LV_TEXT_ALIGN_CENTER,
                    true,
                    false);

    ui_create_button(page.content,
                     261,
                     507,
                     118,
                     48,
                     ui_i18n_pick("录音记录", "Recordings"),
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
