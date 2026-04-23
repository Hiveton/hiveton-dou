#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "drv_lcd.h"
#include "../../sleep_manager.h"
#include "../../xiaozhi/weather/weather.h"

#include <string.h>

lv_obj_t *ui_Standby = NULL;

typedef struct
{
    lv_obj_t *date_label;
    lv_obj_t *lunar_label;
    lv_obj_t *weather_label;
    lv_obj_t *hour_tens_img;
    lv_obj_t *hour_units_img;
    lv_obj_t *colon_img;
    lv_obj_t *minute_tens_img;
    lv_obj_t *minute_units_img;
} ui_standby_refs_t;

typedef struct
{
    char date_text[32];
    char lunar_text[96];
    char weather_text[96];
    char time_text[16];
} ui_standby_snapshot_t;

static ui_standby_refs_t s_refs;
static lv_timer_t *s_refresh_timer = NULL;
static ui_standby_snapshot_t s_last_snapshot;
static bool s_last_snapshot_valid = false;
static char s_cache_date[32];
static char s_cache_lunar[96];
static char s_cache_weather[96];
static char s_cache_time[16];

extern const lv_image_dsc_t img_0;
extern const lv_image_dsc_t img_1;
extern const lv_image_dsc_t img_2;
extern const lv_image_dsc_t img_3;
extern const lv_image_dsc_t img_4;
extern const lv_image_dsc_t img_5;
extern const lv_image_dsc_t img_6;
extern const lv_image_dsc_t img_7;
extern const lv_image_dsc_t img_8;
extern const lv_image_dsc_t img_9;
extern const lv_image_dsc_t second;

static const lv_image_dsc_t *s_time_digits[] = {
    &img_0, &img_1, &img_2, &img_3, &img_4,
    &img_5, &img_6, &img_7, &img_8, &img_9,
};

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
           strcmp(lhs->lunar_text, rhs->lunar_text) == 0 &&
           strcmp(lhs->weather_text, rhs->weather_text) == 0 &&
           strcmp(lhs->time_text, rhs->time_text) == 0;
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
    bool weather_ok = false;
    const char *weekday = "星期四";
    const char *weather_desc = "--";
    int weather_temp = -1000;
    if (snapshot == NULL)
    {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    memset(&current_time, 0, sizeof(current_time));
    memset(&weather, 0, sizeof(weather));

    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        memset(&current_time, 0, sizeof(current_time));
    }
    ui_standby_apply_datetime_fallback(&current_time);

    if (current_time.weekday_str[0] != '\0')
    {
        weekday = ui_i18n_translate_weekday_label(current_time.weekday_str);
    }

    if (xiaozhi_weather_peek(&weather) == RT_EOK && weather.last_update > 0)
    {
        weather_ok = true;
        weather_desc = (weather.text[0] != '\0') ? weather.text : "--";
        weather_temp = weather.temperature;
    }

    rt_snprintf(snapshot->date_text,
                sizeof(snapshot->date_text),
                "%04d/%02d/%02d",
                current_time.year,
                current_time.month,
                current_time.day);
    if (weather_ok && weather.location[0] != '\0')
    {
        rt_snprintf(snapshot->lunar_text,
                    sizeof(snapshot->lunar_text),
                    "农历待同步 · %s",
                    weather.location);
    }
    else
    {
        rt_snprintf(snapshot->lunar_text, sizeof(snapshot->lunar_text), "%s", "农历待同步");
    }

    if (weather_ok)
    {
        rt_snprintf(snapshot->weather_text,
                    sizeof(snapshot->weather_text),
                    "%s  %d°C  %s",
                    weekday,
                    weather_temp,
                    weather_desc);
    }
    else
    {
        rt_snprintf(snapshot->weather_text,
                    sizeof(snapshot->weather_text),
                    "%s  --°C  --",
                    weekday);
    }

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
    ui_standby_set_label(s_refs.lunar_label, s_cache_lunar, sizeof(s_cache_lunar), snapshot.lunar_text);
    ui_standby_set_label(s_refs.weather_label, s_cache_weather, sizeof(s_cache_weather), snapshot.weather_text);
    if (strncmp(s_cache_time, snapshot.time_text, sizeof(s_cache_time)) != 0)
    {
        ui_standby_apply_time_images(snapshot.time_text);
        rt_snprintf(s_cache_time, sizeof(s_cache_time), "%s", snapshot.time_text);
    }

    s_last_snapshot = snapshot;
    s_last_snapshot_valid = true;
    return true;
}

static lv_obj_t *ui_standby_create_card(lv_obj_t *parent,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(card, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
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

    if (ui_Standby != NULL)
    {
        return;
    }

    memset(&s_refs, 0, sizeof(s_refs));
    memset(s_cache_date, 0, sizeof(s_cache_date));
    memset(s_cache_lunar, 0, sizeof(s_cache_lunar));
    memset(s_cache_weather, 0, sizeof(s_cache_weather));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    ui_Standby = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Standby, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_Standby, LV_OPA_COVER, 0);

    s_refs.date_label = ui_create_label(ui_Standby, "2026/01/01", 24, 28, 480, 36, 30, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.lunar_label = ui_create_label(ui_Standby, "农历待同步", 24, 72, 480, 34, 26, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.weather_label = ui_create_label(ui_Standby, "星期四  --°C  --", 24, 114, 480, 34, 26, LV_TEXT_ALIGN_CENTER, false, false);

    s_refs.hour_tens_img = ui_create_image_slot(ui_Standby, 54, 224, 90, 132);
    s_refs.hour_units_img = ui_create_image_slot(ui_Standby, 144, 224, 90, 132);
    s_refs.colon_img = ui_create_image_slot(ui_Standby, 232, 248, 64, 96);
    s_refs.minute_tens_img = ui_create_image_slot(ui_Standby, 294, 224, 90, 132);
    s_refs.minute_units_img = ui_create_image_slot(ui_Standby, 384, 224, 90, 132);
    ui_img_set_src(s_refs.colon_img, &second);

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
    memset(s_cache_lunar, 0, sizeof(s_cache_lunar));
    memset(s_cache_weather, 0, sizeof(s_cache_weather));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    if (ui_Standby != NULL)
    {
        lv_obj_delete(ui_Standby);
        ui_Standby = NULL;
    }
}
