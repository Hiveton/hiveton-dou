#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include <string.h>

lv_obj_t *ui_Language = NULL;

typedef struct
{
    ui_settings_language_t language;
    const char *label;
} ui_language_option_t;

static const char *ui_language_screen_title(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Language";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "语言";
    }
}

static const char *ui_language_screen_hint(void)
{
    return ui_i18n_pick("切换后会立即应用到所有页面。", "Changes apply to all screens immediately.");
}

static void ui_language_invalidate_all_screens(void)
{
    ui_Home_screen_destroy();
    ui_Reading_List_screen_destroy();
    ui_Reading_Detail_screen_destroy();
    ui_Pet_screen_destroy();
    ui_AI_Dou_screen_destroy();
    ui_Time_Manage_screen_destroy();
    ui_Pomodoro_screen_destroy();
    ui_Datetime_screen_destroy();
    ui_Weather_screen_destroy();
    ui_Calendar_screen_destroy();
    ui_Status_Detail_screen_destroy();
    ui_Recorder_screen_destroy();
    ui_Record_List_screen_destroy();
    ui_Music_List_screen_destroy();
    ui_Music_Player_screen_destroy();
    ui_Settings_screen_destroy();
    ui_Sleep_Time_screen_destroy();
    ui_Weather_Toggle_screen_destroy();
    ui_Brightness_screen_destroy();
}

static void ui_language_select_event_cb(lv_event_t *e)
{
    ui_settings_language_t language;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    language = (ui_settings_language_t)(uintptr_t)lv_event_get_user_data(e);
    ui_settings_set_language(language);
    ui_language_invalidate_all_screens();
    ui_runtime_reload(UI_SCREEN_LANGUAGE);
}

void ui_Language_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;
    lv_obj_t *button;
    lv_obj_t *hint;
    size_t i;
    int y = 144;
    ui_settings_language_t selected_language = ui_settings_get_language();
    const ui_language_option_t options[] = {
        {UI_SETTINGS_LANGUAGE_ZH_CN, "简体中文"},
        {UI_SETTINGS_LANGUAGE_EN_US, "English"},
    };

    if (ui_Language != NULL)
    {
        return;
    }

    ui_Language = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Language, ui_language_screen_title(), UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    hint = ui_create_label(panel,
                           ui_language_screen_hint(),
                           44,
                           84,
                           440,
                           40,
                           18,
                           LV_TEXT_ALIGN_LEFT,
                           false,
                           true);
    LV_UNUSED(hint);

    for (i = 0; i < sizeof(options) / sizeof(options[0]); ++i)
    {
        bool is_selected = (options[i].language == selected_language);

        button = ui_create_button(panel, 44, y, 440, 50, options[i].label, 20, UI_SCREEN_NONE, is_selected);
        lv_obj_add_event_cb(button,
                            ui_language_select_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)options[i].language);
        y += 60;
    }
}

void ui_Language_screen_destroy(void)
{
    if (ui_Language != NULL)
    {
        lv_obj_delete(ui_Language);
        ui_Language = NULL;
    }
}
