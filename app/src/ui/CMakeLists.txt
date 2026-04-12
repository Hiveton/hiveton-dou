SET(SOURCES screens/ui_home_screen.c
    screens/ui_standby_screen.c
    screens/ui_reading_list_screen.c
    screens/ui_reading_detail_screen.c
    screens/ui_pet_screen.c
    screens/ui_ai_dou_screen.c
    screens/ui_time_manage_screen.c
    screens/ui_pomodoro_screen.c
    screens/ui_datetime_screen.c
    screens/ui_weather_screen.c
    screens/ui_calendar_screen.c
    screens/ui_recorder_screen.c
    screens/ui_record_list_screen.c
    screens/ui_music_list_screen.c
    screens/ui_music_player_screen.c
    screens/ui_settings_screen.c
    screens/ui_brightness_screen.c
    screens/ui_language_screen.c
    screens/ui_bluetooth_config_screen.c
    screens/ui_wallpaper_screen.c
    home_screen.c
    ui.c
    ui_i18n.c
    ui_helpers.c
    ui_runtime_adapter.c
)

add_library(ui ${SOURCES})
