#ifndef XIAOZHI_FIGMA_UI_H
#define XIAOZHI_FIGMA_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#include "home_screen.h"
#include "ui_events.h"
#include "ui_types.h"

extern lv_obj_t *ui_Home;
extern lv_obj_t *ui_More;
extern lv_obj_t *ui_Standby;
extern lv_obj_t *ui_Reading_List;
extern lv_obj_t *ui_Reading_Detail;
extern lv_obj_t *ui_Reading_Toc;
extern lv_obj_t *ui_Reading_Font;
extern lv_obj_t *ui_Pet;
extern lv_obj_t *ui_Pet_Rules;
extern lv_obj_t *ui_AI_Dou;
extern lv_obj_t *ui_Pomodoro;
extern lv_obj_t *ui_Datetime;
extern lv_obj_t *ui_Weather;
extern lv_obj_t *ui_Calendar;
extern lv_obj_t *ui_Status_Detail;
extern lv_obj_t *ui_About;
extern lv_obj_t *ui_Recorder;
extern lv_obj_t *ui_Record_List;
extern lv_obj_t *ui_Music_List;
extern lv_obj_t *ui_Music_Player;
extern lv_obj_t *ui_Settings;
extern lv_obj_t *ui_Brightness;
extern lv_obj_t *ui_Language;
extern lv_obj_t *ui_Sleep_Time;
extern lv_obj_t *ui_File_Manager;
extern lv_obj_t *ui_File_Manager_Detail;
extern lv_obj_t *ui_Wallpaper;
extern lv_obj_t *ui_AI_Weather_Settings;
extern lv_obj_t *ui_Calculator;

typedef enum
{
    UI_SETTINGS_LANGUAGE_ZH_CN = 0,
    UI_SETTINGS_LANGUAGE_EN_US,
    UI_SETTINGS_LANGUAGE_ZH_TW,
    UI_SETTINGS_LANGUAGE_COUNT
} ui_settings_language_t;

typedef enum
{
    UI_FILE_MANAGER_CATEGORY_FONT = 0,
    UI_FILE_MANAGER_CATEGORY_MUSIC,
    UI_FILE_MANAGER_CATEGORY_RECORD,
    UI_FILE_MANAGER_CATEGORY_READING,
    UI_FILE_MANAGER_CATEGORY_COUNT
} ui_file_manager_category_t;

void ui_Home_screen_init(void);
void ui_Home_screen_destroy(void);
const xiaozhi_home_screen_refs_t *ui_home_screen_refs_get(void);
void ui_home_ai_hardware_talk_press(void);
void ui_home_ai_hardware_talk_release(void);
void ui_More_screen_init(void);
void ui_More_screen_destroy(void);
void ui_Standby_screen_init(void);
void ui_Standby_screen_destroy(void);
void ui_standby_screen_refresh_now(void);

void ui_Reading_List_screen_init(void);
void ui_Reading_List_screen_destroy(void);
const char *ui_reading_list_get_selected_name(void);
bool ui_reading_list_prepare_selected_file(void);
bool ui_reading_list_get_selected_path(char *buffer, size_t buffer_size);
void ui_reading_list_refresh(void);
void ui_reading_list_request_enter_refresh(void);
bool ui_reading_detail_prepare_selected_async(void);
bool ui_reading_detail_is_selected_ready(void);
void ui_Reading_Detail_screen_init(void);
void ui_Reading_Detail_screen_destroy(void);
void ui_Reading_Toc_screen_init(void);
void ui_Reading_Toc_screen_destroy(void);
void ui_reading_toc_hardware_prev_page(void);
void ui_reading_toc_hardware_next_page(void);
void ui_Reading_Font_screen_init(void);
void ui_Reading_Font_screen_destroy(void);
void ui_reading_font_hardware_prev_page(void);
void ui_reading_font_hardware_next_page(void);
uint16_t ui_reading_detail_get_chapter_count(void);
uint16_t ui_reading_detail_get_current_chapter_index(void);
bool ui_reading_detail_get_chapter_title(uint16_t chapter_index, char *buffer, size_t buffer_size);
bool ui_reading_detail_jump_to_chapter(uint16_t chapter_index);
uint16_t ui_reading_detail_get_font_item_count(void);
uint16_t ui_reading_detail_get_current_font_item_index(void);
bool ui_reading_detail_get_font_item_name(uint16_t index, char *buffer, size_t buffer_size);
bool ui_reading_detail_select_font_item(uint16_t index);
void ui_Pet_screen_init(void);
void ui_Pet_screen_destroy(void);
void ui_Pet_Rules_screen_init(void);
void ui_Pet_Rules_screen_destroy(void);
void ui_AI_Dou_screen_init(void);
void ui_AI_Dou_screen_destroy(void);
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
void ui_About_screen_init(void);
void ui_About_screen_destroy(void);
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
void ui_settings_refresh_summaries(void);
void ui_settings_hardware_prev_page(void);
void ui_settings_hardware_next_page(void);
ui_settings_language_t ui_settings_get_language(void);
void ui_settings_set_language(ui_settings_language_t language);
const char *ui_settings_get_language_label(void);
void ui_Brightness_screen_init(void);
void ui_Brightness_screen_destroy(void);
void ui_Language_screen_init(void);
void ui_Language_screen_destroy(void);
void ui_Sleep_Time_screen_init(void);
void ui_Sleep_Time_screen_destroy(void);
void ui_File_Manager_screen_init(void);
void ui_File_Manager_screen_destroy(void);
void ui_File_Manager_Detail_screen_init(void);
void ui_File_Manager_Detail_screen_destroy(void);
void ui_file_manager_detail_set_category(ui_file_manager_category_t category);
void ui_file_manager_detail_hardware_prev_page(void);
void ui_file_manager_detail_hardware_next_page(void);
void ui_Wallpaper_screen_init(void);
void ui_Wallpaper_screen_destroy(void);
void ui_AI_Weather_Settings_screen_init(void);
void ui_AI_Weather_Settings_screen_destroy(void);
void ui_Calculator_screen_init(void);
void ui_Calculator_screen_destroy(void);

ui_screen_id_t ui_rotation_next_screen(ui_screen_id_t current);
void ui_init(void);
void ui_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
