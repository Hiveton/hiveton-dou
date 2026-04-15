#ifndef XIAOZHI_FIGMA_UI_H
#define XIAOZHI_FIGMA_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "lvgl/lvgl.h"

#include "home_screen.h"
#include "ui_events.h"
#include "ui_types.h"

extern lv_obj_t *ui_Home;
extern lv_obj_t *ui_Standby;
extern lv_obj_t *ui_Reading_List;
extern lv_obj_t *ui_Reading_Detail;
extern lv_obj_t *ui_Pet;
extern lv_obj_t *ui_Pet_Rules;
extern lv_obj_t *ui_AI_Dou;
extern lv_obj_t *ui_Time_Manage;
extern lv_obj_t *ui_Pomodoro;
extern lv_obj_t *ui_Datetime;
extern lv_obj_t *ui_Weather;
extern lv_obj_t *ui_Calendar;
extern lv_obj_t *ui_Status_Detail;
extern lv_obj_t *ui_Recorder;
extern lv_obj_t *ui_Record_List;
extern lv_obj_t *ui_Music_List;
extern lv_obj_t *ui_Music_Player;
extern lv_obj_t *ui_Settings;
extern lv_obj_t *ui_Brightness;
extern lv_obj_t *ui_Language;
extern lv_obj_t *ui_Font_Settings;
extern lv_obj_t *ui_Bluetooth_Config;
extern lv_obj_t *ui_Network_Mode;
extern lv_obj_t *ui_Wallpaper;

typedef enum
{
    UI_SETTINGS_LANGUAGE_ZH_CN = 0,
    UI_SETTINGS_LANGUAGE_EN_US,
    UI_SETTINGS_LANGUAGE_COUNT
} ui_settings_language_t;

void ui_Home_screen_init(void);
void ui_Home_screen_destroy(void);
const xiaozhi_home_screen_refs_t *ui_home_screen_refs_get(void);
void ui_Standby_screen_init(void);
void ui_Standby_screen_destroy(void);
void ui_standby_screen_refresh_now(void);

void ui_Reading_List_screen_init(void);
void ui_Reading_List_screen_destroy(void);
const char *ui_reading_list_get_selected_name(void);
bool ui_reading_list_prepare_selected_file(void);
bool ui_reading_list_get_selected_path(char *buffer, size_t buffer_size);
void ui_reading_list_refresh(void);
bool ui_reading_detail_prepare_selected_async(void);
bool ui_reading_detail_is_selected_ready(void);
void ui_Reading_Detail_screen_init(void);
void ui_Reading_Detail_screen_destroy(void);
void ui_Pet_screen_init(void);
void ui_Pet_screen_destroy(void);
void ui_Pet_Rules_screen_init(void);
void ui_Pet_Rules_screen_destroy(void);
void ui_AI_Dou_screen_init(void);
void ui_AI_Dou_screen_destroy(void);
void ui_Time_Manage_screen_init(void);
void ui_Time_Manage_screen_destroy(void);
void ui_time_manage_hardware_prev_page(void);
void ui_time_manage_hardware_next_page(void);
void ui_Pomodoro_screen_init(void);
void ui_Pomodoro_screen_destroy(void);
void ui_Datetime_screen_init(void);
void ui_Datetime_screen_destroy(void);
void ui_datetime_hardware_adjust(int direction);
void ui_Weather_screen_init(void);
void ui_Weather_screen_refresh(void);
void ui_Weather_screen_destroy(void);
void ui_Calendar_screen_init(void);
void ui_Calendar_screen_destroy(void);
void ui_calendar_hardware_prev_month(void);
void ui_calendar_hardware_next_month(void);
void ui_Status_Detail_screen_init(void);
void ui_Status_Detail_screen_destroy(void);
void ui_Status_Detail_screen_set_return_target(ui_screen_id_t target);
void ui_Recorder_screen_init(void);
void ui_Recorder_screen_destroy(void);
void ui_Record_List_screen_init(void);
void ui_Record_List_screen_destroy(void);
void ui_record_list_hardware_prev_page(void);
void ui_record_list_hardware_next_page(void);
void ui_Music_List_screen_init(void);
void ui_Music_List_screen_destroy(void);
void ui_music_list_hardware_prev_page(void);
void ui_music_list_hardware_next_page(void);
void ui_Music_Player_screen_init(void);
void ui_Music_Player_screen_destroy(void);
void ui_Settings_screen_init(void);
void ui_Settings_screen_destroy(void);
void ui_settings_hardware_prev_page(void);
void ui_settings_hardware_next_page(void);
ui_settings_language_t ui_settings_get_language(void);
void ui_settings_set_language(ui_settings_language_t language);
const char *ui_settings_get_language_label(void);
void ui_Brightness_screen_init(void);
void ui_Brightness_screen_destroy(void);
void ui_Language_screen_init(void);
void ui_Language_screen_destroy(void);
void ui_Font_Settings_screen_init(void);
void ui_Font_Settings_screen_destroy(void);
void ui_font_settings_hardware_prev_page(void);
void ui_font_settings_hardware_next_page(void);
void ui_Bluetooth_Config_screen_init(void);
void ui_Bluetooth_Config_screen_destroy(void);
void ui_Network_Mode_screen_init(void);
void ui_Network_Mode_screen_destroy(void);
void ui_network_mode_hardware_prev_option(void);
void ui_network_mode_hardware_next_option(void);
void ui_Wallpaper_screen_init(void);
void ui_Wallpaper_screen_destroy(void);

ui_screen_id_t ui_rotation_next_screen(ui_screen_id_t current);
void ui_init(void);
void ui_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
