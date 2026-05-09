#ifndef XIAOZHI_UI_COMPONENTS_H
#define XIAOZHI_UI_COMPONENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"
#include "ui_types.h"

typedef enum
{
    UI_TOP_TAB_AI = 0,
    UI_TOP_TAB_MUSIC,
    UI_TOP_TAB_WEATHER,
    UI_TOP_TAB_POMODORO,
    UI_TOP_TAB_SETTINGS,
    UI_TOP_TAB_NONE = 255,
} ui_top_tab_t;

typedef enum
{
    UI_BOTTOM_TAB_AI = 0,
    UI_BOTTOM_TAB_PET,
    UI_BOTTOM_TAB_BOOKS,
    UI_BOTTOM_TAB_CALENDAR,
    UI_BOTTOM_TAB_NONE = 255,
} ui_bottom_tab_t;

lv_obj_t *ui_top_nav_create(lv_obj_t *parent, ui_top_tab_t active);
lv_obj_t *ui_bottom_nav_create(lv_obj_t *parent, ui_bottom_tab_t active);
void ui_top_nav_update_battery(uint8_t percent, uint8_t is_charging);

#ifdef __cplusplus
}
#endif

#endif
