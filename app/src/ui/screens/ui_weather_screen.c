#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "ui_dispatch.h"
#include "../../xiaozhi/weather/weather.h"

lv_obj_t *ui_Weather = NULL;

static lv_obj_t *s_weather_location_label = NULL;
static lv_obj_t *s_weather_temp_label = NULL;
static lv_obj_t *s_weather_summary_label = NULL;
static lv_obj_t *s_weather_humidity_label = NULL;
static lv_obj_t *s_weather_wind_label = NULL;
static lv_obj_t *s_weather_feels_like_label = NULL;
static lv_obj_t *s_weather_tip_label = NULL;
static lv_obj_t *s_weather_last_update_label = NULL;
static lv_obj_t *s_weather_icon = NULL;

static void ui_weather_refresh_content(void)
{
    weather_info_t weather = {0};
    char text[96];

    if (ui_Weather == NULL)
    {
        return;
    }

    if (xiaozhi_weather_peek(&weather) != RT_EOK || weather.last_update <= 0)
    {
        if (s_weather_location_label) lv_label_set_text(s_weather_location_label, "天气数据准备中");
        if (s_weather_temp_label) lv_label_set_text(s_weather_temp_label, "--");
        if (s_weather_summary_label) lv_label_set_text(s_weather_summary_label, "联网后将自动同步当前天气");
        if (s_weather_humidity_label) lv_label_set_text(s_weather_humidity_label, "湿度 --");
        if (s_weather_wind_label) lv_label_set_text(s_weather_wind_label, "风向 --");
        if (s_weather_feels_like_label) lv_label_set_text(s_weather_feels_like_label, "体感 --");
        if (s_weather_tip_label) lv_label_set_text(s_weather_tip_label, "可以点击底部按钮手动刷新一次。");
        if (s_weather_last_update_label) lv_label_set_text(s_weather_last_update_label, "上次更新: --:--");
        if (s_weather_icon) lv_img_set_src(s_weather_icon, xiaozhi_weather_get_icon("99"));
        return;
    }

    if (s_weather_location_label)
    {
        lv_label_set_text(s_weather_location_label, weather.location[0] != '\0' ? weather.location : "当前位置");
    }

    if (s_weather_temp_label)
    {
        rt_snprintf(text, sizeof(text), "%d℃", weather.temperature);
        lv_label_set_text(s_weather_temp_label, text);
    }

    if (s_weather_summary_label)
    {
        rt_snprintf(text,
                    sizeof(text),
                    "%s",
                    weather.text[0] != '\0' ? weather.text : "天气未知");
        lv_label_set_text(s_weather_summary_label, text);
    }

    if (s_weather_humidity_label)
    {
        rt_snprintf(text, sizeof(text), "湿度 %d%%", weather.humidity);
        lv_label_set_text(s_weather_humidity_label, text);
    }

    if (s_weather_wind_label)
    {
        rt_snprintf(text,
                    sizeof(text),
                    "%s %s 级",
                    weather.wind_direction[0] != '\0' ? weather.wind_direction : "风向",
                    weather.wind_scale[0] != '\0' ? weather.wind_scale : "--");
        lv_label_set_text(s_weather_wind_label, text);
    }

    if (s_weather_feels_like_label)
    {
        rt_snprintf(text, sizeof(text), "体感 %d℃", weather.feels_like);
        lv_label_set_text(s_weather_feels_like_label, text);
    }

    if (s_weather_tip_label)
    {
        rt_snprintf(text,
                    sizeof(text),
                    "今天%s，出门前记得留意温差变化。",
                    weather.text[0] != '\0' ? weather.text : "天气多变");
        lv_label_set_text(s_weather_tip_label, text);
    }

    if (s_weather_last_update_label)
    {
        struct tm *last_update_tm = localtime(&weather.last_update);

        if (last_update_tm != NULL)
        {
            rt_snprintf(text,
                        sizeof(text),
                        "上次更新: %02d:%02d",
                        last_update_tm->tm_hour,
                        last_update_tm->tm_min);
            lv_label_set_text(s_weather_last_update_label, text);
        }
    }

    if (s_weather_icon)
    {
        lv_img_set_src(s_weather_icon, xiaozhi_weather_get_icon(weather.code));
    }
}

static void ui_weather_refresh_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_weather_last_update_label != NULL)
    {
        lv_label_set_text(s_weather_last_update_label, "上次更新: 刷新中...");
    }

    xiaozhi_weather_request_refresh();
}

void ui_Weather_screen_refresh(void)
{
    ui_weather_refresh_content();
}

void ui_Weather_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *metrics_box;
    lv_obj_t *refresh_button;

    if (ui_Weather != NULL)
    {
        return;
    }

    ui_Weather = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Weather, "天气", UI_SCREEN_HOME);

    s_weather_location_label = ui_create_label(page.content,
                                               "天气数据准备中",
                                               0,
                                               46,
                                               528,
                                               25,
                                               20,
                                               LV_TEXT_ALIGN_CENTER,
                                               false,
                                               false);

    s_weather_icon = ui_create_image_slot(page.content, 196, 85, 136, 136);
    lv_img_set_src(s_weather_icon, xiaozhi_weather_get_icon("99"));

    s_weather_temp_label = ui_create_label(page.content,
                                           "--",
                                           0,
                                           224,
                                           528,
                                           92,
                                           74,
                                           LV_TEXT_ALIGN_CENTER,
                                           false,
                                           false);
    s_weather_summary_label = ui_create_label(page.content,
                                              "联网后将自动同步当前天气",
                                              0,
                                              316,
                                              528,
                                              28,
                                              22,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);

    metrics_box = ui_create_card(page.content, 24, 376, 480, 88, UI_SCREEN_NONE, false, 0);
    s_weather_humidity_label = ui_create_label(metrics_box, "湿度 --", 12, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    s_weather_wind_label = ui_create_label(metrics_box, "风向 --", 164, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    s_weather_feels_like_label = ui_create_label(metrics_box, "体感 --", 316, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);

    s_weather_tip_label = ui_create_label(page.content,
                                          "可以点击底部按钮手动刷新一次。",
                                          41,
                                          492,
                                          446,
                                          34,
                                          20,
                                          LV_TEXT_ALIGN_CENTER,
                                          false,
                                          true);
    s_weather_last_update_label = ui_create_label(page.content,
                                                  "上次更新: --:--",
                                                  0,
                                                  548,
                                                  528,
                                                  24,
                                                  18,
                                                  LV_TEXT_ALIGN_CENTER,
                                                  false,
                                                  false);

    refresh_button = ui_create_button(page.content, 164, 592, 200, 52, "立即刷新", 22, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(refresh_button, ui_weather_refresh_event_cb, LV_EVENT_CLICKED, NULL);

    xiaozhi_weather_request_refresh();
    ui_weather_refresh_content();
}

void ui_Weather_screen_destroy(void)
{
    if (ui_Weather != NULL)
    {
        lv_obj_delete(ui_Weather);
        ui_Weather = NULL;
    }

    s_weather_location_label = NULL;
    s_weather_temp_label = NULL;
    s_weather_summary_label = NULL;
    s_weather_humidity_label = NULL;
    s_weather_wind_label = NULL;
    s_weather_feels_like_label = NULL;
    s_weather_tip_label = NULL;
    s_weather_last_update_label = NULL;
    s_weather_icon = NULL;
}
