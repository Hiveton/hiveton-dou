#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

static bool s_ui_initialized = false;

static const ui_screen_id_t s_ui_rotation_sequence[] = {
    UI_SCREEN_HOME,
    UI_SCREEN_READING_LIST,
    UI_SCREEN_READING_DETAIL,
    UI_SCREEN_PET,
    UI_SCREEN_AI_DOU,
    UI_SCREEN_TIME_MANAGE,
    UI_SCREEN_POMODORO,
    UI_SCREEN_DATETIME,
    UI_SCREEN_WEATHER,
    UI_SCREEN_CALENDAR,
    UI_SCREEN_STATUS_DETAIL,
    UI_SCREEN_RECORDER,
    UI_SCREEN_RECORD_LIST,
    UI_SCREEN_MUSIC_LIST,
    UI_SCREEN_MUSIC_PLAYER,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_BRIGHTNESS,
    UI_SCREEN_LANGUAGE,
    UI_SCREEN_BLUETOOTH_CONFIG,
    UI_SCREEN_WALLPAPER,
};

ui_screen_id_t ui_rotation_next_screen(ui_screen_id_t current)
{
    size_t i;

    for (i = 0; i < sizeof(s_ui_rotation_sequence) / sizeof(s_ui_rotation_sequence[0]); ++i)
    {
        if (s_ui_rotation_sequence[i] == current)
        {
            return s_ui_rotation_sequence[(i + 1U) % (sizeof(s_ui_rotation_sequence) / sizeof(s_ui_rotation_sequence[0]))];
        }
    }

    return s_ui_rotation_sequence[0];
}

void ui_init( void )
{
    lv_disp_t *disp;
    lv_theme_t *theme;

    if (s_ui_initialized)
    {
        return;
    }

    ui_helpers_init();
    disp = lv_display_get_default();
    if (disp != NULL)
    {
        theme = lv_theme_simple_init(disp);
        lv_disp_set_theme(disp, theme);
    }

    s_ui_initialized = true;
}

void ui_destroy( void )
{
    ui_Wallpaper_screen_destroy();
    ui_Standby_screen_destroy();
    ui_Bluetooth_Config_screen_destroy();
    ui_Language_screen_destroy();
    ui_Brightness_screen_destroy();
    ui_Settings_screen_destroy();
    ui_Music_Player_screen_destroy();
    ui_Music_List_screen_destroy();
    ui_Record_List_screen_destroy();
    ui_Recorder_screen_destroy();
    ui_Status_Detail_screen_destroy();
    ui_Calendar_screen_destroy();
    ui_Weather_screen_destroy();
    ui_Datetime_screen_destroy();
    ui_Pomodoro_screen_destroy();
    ui_Time_Manage_screen_destroy();
    ui_AI_Dou_screen_destroy();
    ui_Pet_screen_destroy();
    ui_Reading_Detail_screen_destroy();
    ui_Reading_List_screen_destroy();
    ui_Home_screen_destroy();
    ui_helpers_deinit();
    s_ui_initialized = false;
}
