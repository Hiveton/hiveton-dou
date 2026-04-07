#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "ui_i18n.h"
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

static const char *ui_weather_default_location(void)
{
    return ui_i18n_pick("当前位置", "Current Location");
}

static const char *ui_weather_unknown_text(void)
{
    return ui_i18n_pick("天气未知", "Unknown Weather");
}

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
        if (s_weather_location_label) lv_label_set_text(s_weather_location_label, ui_i18n_pick("天气数据准备中", "Weather is preparing"));
        if (s_weather_temp_label) lv_label_set_text(s_weather_temp_label, "--");
        if (s_weather_summary_label) lv_label_set_text(s_weather_summary_label, ui_i18n_pick("联网后将自动同步当前天气", "Weather syncs automatically when online"));
        if (s_weather_humidity_label) lv_label_set_text(s_weather_humidity_label, ui_i18n_pick("湿度 --", "Humidity --"));
        if (s_weather_wind_label) lv_label_set_text(s_weather_wind_label, ui_i18n_pick("风向 --", "Wind --"));
        if (s_weather_feels_like_label) lv_label_set_text(s_weather_feels_like_label, ui_i18n_pick("体感 --", "Feels Like --"));
        if (s_weather_tip_label) lv_label_set_text(s_weather_tip_label, ui_i18n_pick("可以点击底部按钮手动刷新一次。", "Tap the button below to refresh once."));
        if (s_weather_last_update_label) lv_label_set_text(s_weather_last_update_label, ui_i18n_pick("上次更新: --:--", "Last Update: --:--"));
        if (s_weather_icon) lv_img_set_src(s_weather_icon, xiaozhi_weather_get_icon("99"));
        return;
    }

    if (s_weather_location_label)
    {
        lv_label_set_text(s_weather_location_label, weather.location[0] != '\0' ? weather.location : ui_weather_default_location());
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
                    ui_i18n_translate_weather_text(weather.text[0] != '\0' ? weather.text : ui_weather_unknown_text()));
        lv_label_set_text(s_weather_summary_label, text);
    }

    if (s_weather_humidity_label)
    {
        rt_snprintf(text, sizeof(text), ui_i18n_pick("湿度 %d%%", "Humidity %d%%"), weather.humidity);
        lv_label_set_text(s_weather_humidity_label, text);
    }

    if (s_weather_wind_label)
    {
        rt_snprintf(text,
                    sizeof(text),
                    ui_i18n_pick("%s %s 级", "%s L%s"),
                    weather.wind_direction[0] != '\0' ? weather.wind_direction : ui_i18n_pick("风向", "Wind"),
                    weather.wind_scale[0] != '\0' ? weather.wind_scale : "--");
        lv_label_set_text(s_weather_wind_label, text);
    }

    if (s_weather_feels_like_label)
    {
        rt_snprintf(text, sizeof(text), ui_i18n_pick("体感 %d℃", "Feels Like %dC"), weather.feels_like);
        lv_label_set_text(s_weather_feels_like_label, text);
    }

    if (s_weather_tip_label)
    {
        rt_snprintf(text,
                    sizeof(text),
                    ui_i18n_pick("今天%s，出门前记得留意温差变化。", "%s today, mind the temperature gap before heading out."),
                    ui_i18n_translate_weather_text(weather.text[0] != '\0' ? weather.text : ui_i18n_pick("天气多变", "Changeable weather")));
        lv_label_set_text(s_weather_tip_label, text);
    }

    if (s_weather_last_update_label)
    {
        struct tm *last_update_tm = localtime(&weather.last_update);

        if (last_update_tm != NULL)
        {
            rt_snprintf(text,
                        sizeof(text),
                        ui_i18n_pick("上次更新: %02d:%02d", "Last Update: %02d:%02d"),
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
        lv_label_set_text(s_weather_last_update_label, ui_i18n_pick("上次更新: 刷新中...", "Last Update: Refreshing..."));
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
    ui_build_standard_screen(&page, ui_Weather, ui_i18n_pick("天气", "Weather"), UI_SCREEN_HOME);

    s_weather_location_label = ui_create_label(page.content,
                                               ui_i18n_pick("天气数据准备中", "Weather is preparing"),
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
                                              ui_i18n_pick("联网后将自动同步当前天气", "Weather syncs automatically when online"),
                                              0,
                                              316,
                                              528,
                                              28,
                                              22,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);

    metrics_box = ui_create_card(page.content, 24, 376, 480, 88, UI_SCREEN_NONE, false, 0);
    s_weather_humidity_label = ui_create_label(metrics_box, ui_i18n_pick("湿度 --", "Humidity --"), 12, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    s_weather_wind_label = ui_create_label(metrics_box, ui_i18n_pick("风向 --", "Wind --"), 164, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);
    s_weather_feels_like_label = ui_create_label(metrics_box, ui_i18n_pick("体感 --", "Feels Like --"), 316, 30, 152, 20, 16, LV_TEXT_ALIGN_CENTER, false, false);

    s_weather_tip_label = ui_create_label(page.content,
                                          ui_i18n_pick("可以点击底部按钮手动刷新一次。", "Tap the button below to refresh once."),
                                          41,
                                          492,
                                          446,
                                          34,
                                          20,
                                          LV_TEXT_ALIGN_CENTER,
                                          false,
                                          true);
    s_weather_last_update_label = ui_create_label(page.content,
                                                  ui_i18n_pick("上次更新: --:--", "Last Update: --:--"),
                                                  0,
                                                  548,
                                                  528,
                                                  24,
                                                  18,
                                                  LV_TEXT_ALIGN_CENTER,
                                                  false,
                                                  false);

    refresh_button = ui_create_button(page.content, 164, 592, 200, 52, ui_i18n_pick("立即刷新", "Refresh"), 22, UI_SCREEN_NONE, true);
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
