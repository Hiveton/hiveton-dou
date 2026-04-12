#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "../../xiaozhi/weather/weather.h"

#include <string.h>

lv_obj_t *ui_Standby = NULL;

typedef struct
{
    lv_obj_t *date_label;
    lv_obj_t *lunar_label;
    lv_obj_t *weather_label;
    lv_obj_t *indoor_label;
    lv_obj_t *time_label;
    lv_obj_t *alarm_label;
    lv_obj_t *memo_label;
    lv_obj_t *quote_label;
    lv_obj_t *quote_author_label;
} ui_standby_refs_t;

static ui_standby_refs_t s_refs;
static lv_timer_t *s_refresh_timer = NULL;
static char s_cache_date[32];
static char s_cache_lunar[96];
static char s_cache_weather[96];
static char s_cache_indoor[96];
static char s_cache_time[16];
static char s_cache_alarm[24];

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

static void ui_standby_refresh_content(void)
{
    date_time_t current_time;
    weather_info_t weather;
    bool weather_ok = false;
    char date_text[32];
    char lunar_text[96];
    char weather_text[96];
    char indoor_text[96];
    char time_text[16];
    char alarm_text[24];
    const char *weekday = "星期四";
    const char *weather_desc = "--";
    int weather_temp = -1000;
    int indoor_temp = -1000;
    int humidity = -1;

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
        indoor_temp = (weather.feels_like != 0) ? weather.feels_like : weather.temperature;
        humidity = weather.humidity;
    }

    rt_snprintf(date_text,
                sizeof(date_text),
                "%04d/%02d/%02d",
                current_time.year,
                current_time.month,
                current_time.day);
    if (weather_ok && weather.location[0] != '\0')
    {
        rt_snprintf(lunar_text,
                    sizeof(lunar_text),
                    "农历待同步 · %s",
                    weather.location);
    }
    else
    {
        rt_snprintf(lunar_text, sizeof(lunar_text), "%s", "农历待同步");
    }

    if (weather_ok)
    {
        rt_snprintf(weather_text,
                    sizeof(weather_text),
                    "%s  %d°C  %s",
                    weekday,
                    weather_temp,
                    weather_desc);
        rt_snprintf(indoor_text,
                    sizeof(indoor_text),
                    "室内：%d°C    %d%%",
                    indoor_temp,
                    humidity);
    }
    else
    {
        rt_snprintf(weather_text,
                    sizeof(weather_text),
                    "%s  --°C  --",
                    weekday);
        rt_snprintf(indoor_text,
                    sizeof(indoor_text),
                    "%s",
                    "室内：--°C    --%");
    }

    rt_snprintf(time_text,
                sizeof(time_text),
                "%02d:%02d",
                current_time.hour,
                current_time.minute);
    rt_snprintf(alarm_text, sizeof(alarm_text), "%s", "闹钟 7:30");

    ui_standby_set_label(s_refs.date_label, s_cache_date, sizeof(s_cache_date), date_text);
    ui_standby_set_label(s_refs.lunar_label, s_cache_lunar, sizeof(s_cache_lunar), lunar_text);
    ui_standby_set_label(s_refs.weather_label, s_cache_weather, sizeof(s_cache_weather), weather_text);
    ui_standby_set_label(s_refs.indoor_label, s_cache_indoor, sizeof(s_cache_indoor), indoor_text);
    ui_standby_set_label(s_refs.time_label, s_cache_time, sizeof(s_cache_time), time_text);
    ui_standby_set_label(s_refs.alarm_label, s_cache_alarm, sizeof(s_cache_alarm), alarm_text);
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

void ui_Standby_screen_init(void)
{
    lv_obj_t *divider;
    lv_obj_t *card;
    lv_obj_t *overlay;

    if (ui_Standby != NULL)
    {
        return;
    }

    memset(&s_refs, 0, sizeof(s_refs));
    memset(s_cache_date, 0, sizeof(s_cache_date));
    memset(s_cache_lunar, 0, sizeof(s_cache_lunar));
    memset(s_cache_weather, 0, sizeof(s_cache_weather));
    memset(s_cache_indoor, 0, sizeof(s_cache_indoor));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(s_cache_alarm, 0, sizeof(s_cache_alarm));

    ui_Standby = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Standby, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_Standby, LV_OPA_COVER, 0);

    s_refs.date_label = ui_create_label(ui_Standby, "2026/01/01", 22, 18, 480, 36, 30, LV_TEXT_ALIGN_LEFT, false, false);
    s_refs.lunar_label = ui_create_label(ui_Standby, "农历待同步", 22, 56, 480, 38, 28, LV_TEXT_ALIGN_LEFT, false, false);
    s_refs.weather_label = ui_create_label(ui_Standby, "星期四  --°C  --", 22, 100, 480, 36, 28, LV_TEXT_ALIGN_LEFT, false, false);

    divider = lv_obj_create(ui_Standby);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(divider, 0, ui_px_y(154));
    lv_obj_set_size(divider, ui_px_w(528), 2);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);
    lv_obj_set_style_outline_width(divider, 0, 0);

    s_refs.indoor_label = ui_create_label(ui_Standby, "室内：--°C    --%", 36, 172, 456, 34, 26, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.time_label = ui_create_label(ui_Standby, "01:01", 0, 226, 528, 128, 114, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.alarm_label = ui_create_label(ui_Standby, "闹钟 7:30", 0, 364, 528, 36, 30, LV_TEXT_ALIGN_CENTER, false, false);

    card = ui_standby_create_card(ui_Standby, 20, 430, 488, 96);
    s_refs.memo_label = ui_create_label(card,
                                        "备忘：明天早上给老婆做双蛋三明治\n加红米粥",
                                        20,
                                        16,
                                        448,
                                        64,
                                        24,
                                        LV_TEXT_ALIGN_CENTER,
                                        false,
                                        true);

    card = ui_standby_create_card(ui_Standby, 20, 548, 488, 116);
    s_refs.quote_label = ui_create_label(card,
                                         "末日来临前我又度过了美好的一天！",
                                         20,
                                         16,
                                         448,
                                         36,
                                         24,
                                         LV_TEXT_ALIGN_CENTER,
                                         false,
                                         true);
    s_refs.quote_author_label = ui_create_label(card,
                                                "—— 卧豆",
                                                0,
                                                60,
                                                448,
                                                28,
                                                24,
                                                LV_TEXT_ALIGN_RIGHT,
                                                false,
                                                false);

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
    memset(s_cache_indoor, 0, sizeof(s_cache_indoor));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(s_cache_alarm, 0, sizeof(s_cache_alarm));

    if (ui_Standby != NULL)
    {
        lv_obj_delete(ui_Standby);
        ui_Standby = NULL;
    }
}
