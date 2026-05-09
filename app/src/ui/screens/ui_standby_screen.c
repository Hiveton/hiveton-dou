#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "drv_lcd.h"
#include "../../sleep_manager.h"
#include "../../bq27220_monitor.h"
#include "../../xiaozhi/weather/weather.h"

#include <string.h>

lv_obj_t *ui_Standby = NULL;

typedef struct
{
    lv_obj_t *date_label;
    lv_obj_t *weather_label;
    lv_obj_t *battery_label;
    lv_obj_t *weather_icon_img;
    lv_obj_t *hour_tens_img;
    lv_obj_t *hour_units_img;
    lv_obj_t *colon_img;
    lv_obj_t *minute_tens_img;
    lv_obj_t *minute_units_img;
} ui_standby_refs_t;

typedef struct
{
    char date_text[32];
    char weather_text[96];
    char battery_text[12];
    char time_text[16];
} ui_standby_snapshot_t;

static ui_standby_refs_t s_refs;
static lv_timer_t *s_refresh_timer = NULL;
static ui_standby_snapshot_t s_last_snapshot;
static bool s_last_snapshot_valid = false;
static char s_cache_date[32];
static char s_cache_weather[96];
static char s_cache_battery[12];
static char s_cache_time[16];

extern const lv_image_dsc_t standby_static;
extern const lv_image_dsc_t standby_digit_0;
extern const lv_image_dsc_t standby_digit_1;
extern const lv_image_dsc_t standby_digit_2;
extern const lv_image_dsc_t standby_digit_3;
extern const lv_image_dsc_t standby_digit_4;
extern const lv_image_dsc_t standby_digit_5;
extern const lv_image_dsc_t standby_digit_6;
extern const lv_image_dsc_t standby_digit_7;
extern const lv_image_dsc_t standby_digit_8;
extern const lv_image_dsc_t standby_digit_9;
extern const lv_image_dsc_t standby_colon;
extern const lv_image_dsc_t standby_weather_sunny;
extern const lv_image_dsc_t standby_weather_cloudy;
extern const lv_image_dsc_t standby_weather_overcast;
extern const lv_image_dsc_t standby_weather_rain;
extern const lv_image_dsc_t standby_weather_snow;
extern const lv_image_dsc_t standby_weather_fog;

static const lv_image_dsc_t *s_time_digits[] = {
    &standby_digit_0, &standby_digit_1, &standby_digit_2, &standby_digit_3, &standby_digit_4,
    &standby_digit_5, &standby_digit_6, &standby_digit_7, &standby_digit_8, &standby_digit_9,
};

static const lv_image_dsc_t *ui_standby_pick_weather_icon(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return &standby_weather_sunny;
    }
    if (strstr(text, "雪") != NULL)
    {
        return &standby_weather_snow;
    }
    if (strstr(text, "雨") != NULL || strstr(text, "雷") != NULL)
    {
        return &standby_weather_rain;
    }
    if (strstr(text, "雾") != NULL || strstr(text, "霾") != NULL || strstr(text, "沙") != NULL)
    {
        return &standby_weather_fog;
    }
    if (strstr(text, "阴") != NULL)
    {
        return &standby_weather_overcast;
    }
    if (strstr(text, "云") != NULL)
    {
        return &standby_weather_cloudy;
    }
    return &standby_weather_sunny;
}

static void ui_standby_apply_weather_icon(const char *text)
{
    if (s_refs.weather_icon_img == NULL)
    {
        return;
    }
    ui_img_set_src(s_refs.weather_icon_img, ui_standby_pick_weather_icon(text));
}

static uint32_t ui_standby_next_refresh_delay_ms(void)
{
    date_time_t current_time;
    uint32_t delay_ms = 60000U;

    if (xiaozhi_time_get_current(&current_time) == RT_EOK)
    {
        int second = current_time.second;

        if (second < 0)
        {
            second = 0;
        }
        if (second > 59)
        {
            second = 59;
        }

        delay_ms = (uint32_t)(60 - second) * 1000U;
        if (delay_ms == 0U)
        {
            delay_ms = 60000U;
        }
    }

    return delay_ms;
}

static void ui_standby_set_label(lv_obj_t *label,
                                 char *cache,
                                 size_t cache_size,
                                 const char *text)
{
    if (label == NULL || cache == NULL || cache_size == 0U || text == NULL)
    {
        return;
    }

    if (strncmp(cache, text, cache_size) == 0)
    {
        return;
    }

    lv_label_set_text(label, text);
    rt_snprintf(cache, cache_size, "%s", text);
}

static bool ui_standby_snapshot_same(const ui_standby_snapshot_t *lhs,
                                     const ui_standby_snapshot_t *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return false;
    }

    return strcmp(lhs->date_text, rhs->date_text) == 0 &&
           strcmp(lhs->weather_text, rhs->weather_text) == 0 &&
           strcmp(lhs->battery_text, rhs->battery_text) == 0 &&
           strcmp(lhs->time_text, rhs->time_text) == 0;
}

static void ui_standby_format_weekday_short(const char *weekday,
                                            char *buffer,
                                            size_t buffer_size)
{
    const char *translated;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    translated = ui_i18n_translate_weekday_label(weekday);
    if (translated == NULL || translated[0] == '\0')
    {
        rt_snprintf(buffer, buffer_size, "%s", "周?");
        return;
    }

    if (strncmp(translated, "星期", strlen("星期")) == 0)
    {
        rt_snprintf(buffer, buffer_size, "周%s", translated + strlen("星期"));
    }
    else if (strncmp(translated, "周", strlen("周")) == 0)
    {
        rt_snprintf(buffer, buffer_size, "%s", translated);
    }
    else if (strlen(translated) <= 4U)
    {
        rt_snprintf(buffer, buffer_size, "周%s", translated);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s", translated);
    }
}

static void ui_standby_apply_datetime_fallback(date_time_t *time_info)
{
    if (time_info == NULL)
    {
        return;
    }

    if (time_info->year >= 2026 &&
        time_info->month >= 1 && time_info->month <= 12 &&
        time_info->day >= 1 && time_info->day <= 31 &&
        time_info->hour >= 0 && time_info->hour <= 23 &&
        time_info->minute >= 0 && time_info->minute <= 59)
    {
        return;
    }

    memset(time_info, 0, sizeof(*time_info));
    time_info->year = 2026;
    time_info->month = 1;
    time_info->day = 1;
    time_info->hour = 1;
    time_info->minute = 1;
    time_info->second = 0;
    time_info->weekday = 4;
    rt_snprintf(time_info->weekday_str, sizeof(time_info->weekday_str), "%s", "星期四");
}

static void ui_standby_build_snapshot(ui_standby_snapshot_t *snapshot)
{
    date_time_t current_time;
    weather_info_t weather;
    bq27220_power_snapshot_t power_snapshot;
    bool weather_ok = false;
    const char *weekday = "星期四";
    char weekday_short[16];
    const char *weather_location = "深圳";
    const char *weather_desc = "--";
    int weather_temp = -1000;
    int battery_percent = 89;
    if (snapshot == NULL)
    {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    memset(&current_time, 0, sizeof(current_time));
    memset(&weather, 0, sizeof(weather));
    memset(&power_snapshot, 0, sizeof(power_snapshot));

    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        memset(&current_time, 0, sizeof(current_time));
    }
    ui_standby_apply_datetime_fallback(&current_time);

    if (current_time.weekday_str[0] != '\0')
    {
        weekday = current_time.weekday_str;
    }
    ui_standby_format_weekday_short(weekday, weekday_short, sizeof(weekday_short));

    if (xiaozhi_weather_peek(&weather) == RT_EOK && weather.last_update > 0)
    {
        weather_ok = true;
        weather_location = (weather.location[0] != '\0') ? weather.location : weather_location;
        weather_desc = (weather.text[0] != '\0') ? ui_i18n_translate_weather_text(weather.text) : "--";
        weather_temp = weather.temperature;
    }

    rt_snprintf(snapshot->date_text,
                sizeof(snapshot->date_text),
                "%02d/%02d %s",
                current_time.month,
                current_time.day,
                weekday_short);

    if (weather_ok)
    {
        rt_snprintf(snapshot->weather_text,
                    sizeof(snapshot->weather_text),
                    "%s  %d°C  %s",
                    weather_location,
                    weather_temp,
                    weather_desc);
    }
    else
    {
        rt_snprintf(snapshot->weather_text,
                    sizeof(snapshot->weather_text),
                    "%s  --°C  --",
                    weather_location);
    }

    bq27220_monitor_get_power_snapshot(&power_snapshot);
    if (power_snapshot.valid)
    {
        battery_percent = (int)power_snapshot.battery_percent;
        if (battery_percent < 0)
        {
            battery_percent = 0;
        }
        if (battery_percent > 100)
        {
            battery_percent = 100;
        }
    }
    rt_snprintf(snapshot->battery_text, sizeof(snapshot->battery_text), "%d%%", battery_percent);

    rt_snprintf(snapshot->time_text,
                sizeof(snapshot->time_text),
                "%02d:%02d",
                current_time.hour,
                current_time.minute);
}

static void ui_standby_apply_time_images(const char *time_text)
{
    int hour_tens;
    int hour_units;
    int minute_tens;
    int minute_units;

    if (time_text == NULL || strlen(time_text) < 5U)
    {
        return;
    }

    hour_tens = time_text[0] - '0';
    hour_units = time_text[1] - '0';
    minute_tens = time_text[3] - '0';
    minute_units = time_text[4] - '0';

    if (hour_tens < 0 || hour_tens > 9 ||
        hour_units < 0 || hour_units > 9 ||
        minute_tens < 0 || minute_tens > 9 ||
        minute_units < 0 || minute_units > 9)
    {
        return;
    }

    if (s_refs.hour_tens_img != NULL) ui_img_set_src(s_refs.hour_tens_img, s_time_digits[hour_tens]);
    if (s_refs.hour_units_img != NULL) ui_img_set_src(s_refs.hour_units_img, s_time_digits[hour_units]);
    if (s_refs.minute_tens_img != NULL) ui_img_set_src(s_refs.minute_tens_img, s_time_digits[minute_tens]);
    if (s_refs.minute_units_img != NULL) ui_img_set_src(s_refs.minute_units_img, s_time_digits[minute_units]);
}

static bool ui_standby_refresh_content(void)
{
    ui_standby_snapshot_t snapshot;

    ui_standby_build_snapshot(&snapshot);
    if (s_last_snapshot_valid && ui_standby_snapshot_same(&snapshot, &s_last_snapshot))
    {
        return false;
    }

    ui_standby_set_label(s_refs.date_label, s_cache_date, sizeof(s_cache_date), snapshot.date_text);
    ui_standby_set_label(s_refs.weather_label, s_cache_weather, sizeof(s_cache_weather), snapshot.weather_text);
    ui_standby_set_label(s_refs.battery_label, s_cache_battery, sizeof(s_cache_battery), snapshot.battery_text);
    ui_standby_apply_weather_icon(snapshot.weather_text);
    if (strncmp(s_cache_time, snapshot.time_text, sizeof(s_cache_time)) != 0)
    {
        ui_standby_apply_time_images(snapshot.time_text);
        rt_snprintf(s_cache_time, sizeof(s_cache_time), "%s", snapshot.time_text);
    }

    s_last_snapshot = snapshot;
    s_last_snapshot_valid = true;
    return true;
}

static void ui_standby_prepare_scaled_image(lv_obj_t *img,
                                            const lv_image_dsc_t *src,
                                            int32_t scale_x,
                                            int32_t scale_y)
{
    if (img == NULL)
    {
        return;
    }

    if (src != NULL)
    {
        lv_image_set_src(img, src);
    }
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_antialias(img, false);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_scale_x(img, (uint32_t)scale_x);
    lv_image_set_scale_y(img, (uint32_t)scale_y);
}

static void ui_standby_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (ui_runtime_get_active_screen_id() != UI_SCREEN_STANDBY)
    {
        return;
    }

    ui_standby_refresh_content();
    lv_timer_set_period(s_refresh_timer, ui_standby_next_refresh_delay_ms());
}

static void ui_standby_exit_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    ui_runtime_exit_standby();
}

void ui_standby_screen_refresh_now(void)
{
    if (ui_runtime_get_active_screen_id() != UI_SCREEN_STANDBY)
    {
        return;
    }

    ui_standby_refresh_content();
    if (s_refresh_timer != NULL)
    {
        lv_timer_set_period(s_refresh_timer, ui_standby_next_refresh_delay_ms());
    }

    lv_refr_now(NULL);
    sleep_manager_resume_sleep_cycle();
}

void ui_Standby_screen_init(void)
{
    lv_obj_t *overlay;
    lv_obj_t *static_bg;

    if (ui_Standby != NULL)
    {
        return;
    }

    memset(&s_refs, 0, sizeof(s_refs));
    memset(s_cache_date, 0, sizeof(s_cache_date));
    memset(s_cache_weather, 0, sizeof(s_cache_weather));
    memset(s_cache_battery, 0, sizeof(s_cache_battery));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    ui_Standby = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Standby, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_Standby, LV_OPA_COVER, 0);

    static_bg = ui_create_image_slot(ui_Standby, 0, 0, UI_FIGMA_WIDTH, UI_FIGMA_HEIGHT);
    ui_img_set_src(static_bg, &standby_static);

    s_refs.battery_label = ui_create_label(ui_Standby, "89%", 408, 32, 72, 40, 32, LV_TEXT_ALIGN_CENTER, false, false);

    s_refs.hour_tens_img = ui_create_image_slot(ui_Standby, 28, 158, 104, 236);
    s_refs.hour_units_img = ui_create_image_slot(ui_Standby, 145, 158, 104, 236);
    s_refs.colon_img = ui_create_image_slot(ui_Standby, 250, 206, 40, 140);
    s_refs.minute_tens_img = ui_create_image_slot(ui_Standby, 300, 158, 104, 236);
    s_refs.minute_units_img = ui_create_image_slot(ui_Standby, 412, 158, 104, 236);
    ui_standby_prepare_scaled_image(s_refs.hour_tens_img, NULL, 256, 256);
    ui_standby_prepare_scaled_image(s_refs.hour_units_img, NULL, 256, 256);
    ui_standby_prepare_scaled_image(s_refs.colon_img, &standby_colon, 256, 256);
    ui_standby_prepare_scaled_image(s_refs.minute_tens_img, NULL, 256, 256);
    ui_standby_prepare_scaled_image(s_refs.minute_units_img, NULL, 256, 256);

    s_refs.date_label = ui_create_label(ui_Standby, "04/28 周二", 84, 400, 360, 64, 50, LV_TEXT_ALIGN_CENTER, false, false);
    lv_label_set_long_mode(s_refs.date_label, LV_LABEL_LONG_DOT);

    s_refs.weather_icon_img = ui_create_image_slot(ui_Standby, 56, 524, 84, 84);
    ui_img_set_src(s_refs.weather_icon_img, &standby_weather_sunny);

    s_refs.weather_label = ui_create_label(ui_Standby, "深圳 --°C --", 184, 520, 296, 70, 44, LV_TEXT_ALIGN_CENTER, false, false);
    lv_label_set_long_mode(s_refs.weather_label, LV_LABEL_LONG_DOT);

    overlay = lv_obj_create(ui_Standby);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, ui_px_w(UI_FIGMA_WIDTH), ui_px_h(UI_FIGMA_HEIGHT));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_shadow_width(overlay, 0, 0);
    lv_obj_set_style_outline_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_add_event_cb(overlay, ui_standby_exit_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(overlay);

    if (s_refresh_timer == NULL)
    {
        s_refresh_timer = lv_timer_create(ui_standby_refresh_timer_cb,
                                          ui_standby_next_refresh_delay_ms(),
                                          NULL);
    }

    ui_standby_refresh_content();
}

void ui_Standby_screen_destroy(void)
{
    if (s_refresh_timer != NULL)
    {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    memset(&s_refs, 0, sizeof(s_refs));
    memset(s_cache_date, 0, sizeof(s_cache_date));
    memset(s_cache_weather, 0, sizeof(s_cache_weather));
    memset(s_cache_battery, 0, sizeof(s_cache_battery));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    if (ui_Standby != NULL)
    {
        lv_obj_delete(ui_Standby);
        ui_Standby = NULL;
    }
}
