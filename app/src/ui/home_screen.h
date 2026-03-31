#ifndef XIAOZHI_HOME_SCREEN_H
#define XIAOZHI_HOME_SCREEN_H

#include <stdbool.h>

#include "lvgl.h"
#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *time_label;
    lv_obj_t *meta_label;
    lv_obj_t *img_emoji;
    lv_obj_t *hour_tens_img;
    lv_obj_t *hour_units_img;
    lv_obj_t *minute_tens_img;
    lv_obj_t *minute_units_img;
    lv_obj_t *bluetooth_icon;
    lv_obj_t *network_icon;
    lv_obj_t *battery_arc;
    lv_obj_t *battery_percent_label;
    lv_obj_t *standby_charging_icon;
    lv_obj_t *weather_icon;
    lv_obj_t *ui_Label_ip;
    lv_obj_t *last_time;
    lv_obj_t *ui_Image_calendar;
    lv_obj_t *ui_Label_year;
    lv_obj_t *ui_Label_lunar;
    lv_obj_t *ui_Label_day;
    lv_obj_t *ui_Label_second;
    lv_obj_t *ui_Image_second;
    lv_obj_t *ui_Label3;
    lv_obj_t *tf_dir_label;
    lv_obj_t *ec800_status_label;
} xiaozhi_home_screen_refs_t;

typedef struct
{
    lv_obj_t *home_screen;
    lv_obj_t *standby_screen;
    xiaozhi_home_screen_refs_t home_refs;
    xiaozhi_home_screen_refs_t standby_refs;
} home_screen_bundle_t;

rt_err_t home_screen_build(lv_event_cb_t home_event_cb,
                           bool enable_home_event,
                           home_screen_bundle_t *bundle);
lv_font_t *home_screen_font_get(uint16_t size);
lv_font_t *home_screen_title_font_get(uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
