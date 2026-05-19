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
    lv_obj_t *time_label;
    lv_obj_t *date_weather_label;
    lv_obj_t *alarm_label;
    lv_obj_t *memo_label;
    lv_obj_t *room_temp_value_label;
    lv_obj_t *humidity_value_label;
    lv_obj_t *battery_value_label;
} ui_standby_refs_t;

typedef struct
{
    char time_text[16];
    char date_weather_text[80];
    char room_temp_text[16];
    char humidity_text[16];
    char battery_text[16];
} ui_standby_snapshot_t;

static ui_standby_refs_t s_refs;
static lv_timer_t *s_refresh_timer = NULL;
static ui_standby_snapshot_t s_last_snapshot;
static bool s_last_snapshot_valid = false;
static char s_cache_time[16];
static char s_cache_date_weather[80];
static char s_cache_room_temp[16];
static char s_cache_humidity[16];
static char s_cache_battery[16];

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

    return strcmp(lhs->time_text, rhs->time_text) == 0 &&
           strcmp(lhs->date_weather_text, rhs->date_weather_text) == 0 &&
           strcmp(lhs->room_temp_text, rhs->room_temp_text) == 0 &&
           strcmp(lhs->humidity_text, rhs->humidity_text) == 0 &&
           strcmp(lhs->battery_text, rhs->battery_text) == 0;
}

static void ui_standby_format_weekday_full(const char *weekday,
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
        rt_snprintf(buffer, buffer_size, "%s", "星期四");
        return;
    }

    if (strncmp(translated, "周", strlen("周")) == 0)
    {
        rt_snprintf(buffer, buffer_size, "星期%s", translated + strlen("周"));
        return;
    }

    rt_snprintf(buffer, buffer_size, "%s", translated);
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
    time_info->month = 5;
    time_info->day = 25;
    time_info->hour = 20;
    time_info->minute = 36;
    time_info->second = 0;
    time_info->weekday = 4;
    rt_snprintf(time_info->weekday_str, sizeof(time_info->weekday_str), "%s", "星期四");
}

static void ui_standby_build_snapshot(ui_standby_snapshot_t *snapshot)
{
    date_time_t current_time;
    weather_info_t weather;
    bq27220_power_snapshot_t power_snapshot;
    const char *weekday = "星期四";
    const char *weather_desc = "晴";
    char weekday_full[16];
    int room_temp = 25;
    int humidity = 49;
    int battery_percent = 78;

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
    ui_standby_format_weekday_full(weekday, weekday_full, sizeof(weekday_full));

    if (xiaozhi_weather_peek(&weather) == RT_EOK && weather.last_update > 0)
    {
        if (weather.text[0] != '\0')
        {
            weather_desc = ui_i18n_translate_weather_text(weather.text);
        }
        room_temp = weather.temperature;
        if (weather.humidity >= 0 && weather.humidity <= 100)
        {
            humidity = weather.humidity;
        }
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

    rt_snprintf(snapshot->time_text,
                sizeof(snapshot->time_text),
                "%02d:%02d",
                current_time.hour,
                current_time.minute);
    rt_snprintf(snapshot->date_weather_text,
                sizeof(snapshot->date_weather_text),
                "%02d-%02d %s  %s %d℃",
                current_time.month,
                current_time.day,
                weekday_full,
                weather_desc,
                room_temp);
    rt_snprintf(snapshot->room_temp_text, sizeof(snapshot->room_temp_text), "%d℃", room_temp);
    rt_snprintf(snapshot->humidity_text, sizeof(snapshot->humidity_text), "%d%%", humidity);
    rt_snprintf(snapshot->battery_text, sizeof(snapshot->battery_text), "%d%%", battery_percent);
}

static bool ui_standby_refresh_content(void)
{
    ui_standby_snapshot_t snapshot;

    ui_standby_build_snapshot(&snapshot);
    if (s_last_snapshot_valid && ui_standby_snapshot_same(&snapshot, &s_last_snapshot))
    {
        return false;
    }

    ui_standby_set_label(s_refs.time_label, s_cache_time, sizeof(s_cache_time), snapshot.time_text);
    ui_standby_set_label(s_refs.date_weather_label,
                         s_cache_date_weather,
                         sizeof(s_cache_date_weather),
                         snapshot.date_weather_text);
    ui_standby_set_label(s_refs.room_temp_value_label,
                         s_cache_room_temp,
                         sizeof(s_cache_room_temp),
                         snapshot.room_temp_text);
    ui_standby_set_label(s_refs.humidity_value_label,
                         s_cache_humidity,
                         sizeof(s_cache_humidity),
                         snapshot.humidity_text);
    ui_standby_set_label(s_refs.battery_value_label,
                         s_cache_battery,
                         sizeof(s_cache_battery),
                         snapshot.battery_text);

    s_last_snapshot = snapshot;
    s_last_snapshot_valid = true;
    return true;
}

static lv_obj_t *ui_standby_plain_obj(lv_obj_t *parent,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      uint32_t border_width,
                                      uint32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, ui_px_w((int32_t)border_width), 0);
    lv_obj_set_style_radius(obj, ui_px_w((int32_t)radius), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_standby_style_label_color(lv_obj_t *label)
{
    if (label != NULL)
    {
        lv_obj_set_style_text_color(label, lv_color_hex(0x343434), 0);
    }
}

static void ui_standby_create_alarm_icon(lv_obj_t *parent)
{
    lv_obj_t *face = ui_standby_plain_obj(parent, 195, 320, 55, 55, 3, 28);
    lv_obj_t *hand_v = ui_standby_plain_obj(face, 25, 12, 3, 18, 0, 2);
    lv_obj_t *hand_h = ui_standby_plain_obj(face, 27, 27, 12, 3, 0, 2);
    lv_obj_t *foot_l = ui_standby_plain_obj(parent, 202, 372, 12, 3, 0, 2);
    lv_obj_t *foot_r = ui_standby_plain_obj(parent, 231, 372, 12, 3, 0, 2);
    LV_UNUSED(hand_v);
    LV_UNUSED(hand_h);
    LV_UNUSED(foot_l);
    LV_UNUSED(foot_r);
}

static void ui_standby_create_temp_icon(lv_obj_t *parent)
{
    lv_obj_t *tube = ui_standby_plain_obj(parent, 126, 635, 10, 26, 2, 5);
    lv_obj_t *bulb = ui_standby_plain_obj(parent, 121, 654, 20, 20, 2, 10);
    lv_obj_t *dot = ui_standby_plain_obj(parent, 130, 641, 2, 17, 0, 1);
    LV_UNUSED(tube);
    LV_UNUSED(bulb);
    LV_UNUSED(dot);
}

static void ui_standby_create_humidity_icon(lv_obj_t *parent)
{
    lv_obj_t *drop = ui_standby_plain_obj(parent, 247, 630, 26, 36, 2, 13);
    lv_obj_set_style_radius(drop, ui_px_w(18), 0);
    lv_obj_t *cut = ui_standby_plain_obj(parent, 247, 630, 13, 13, 0, 0);
    lv_obj_set_style_bg_color(cut, lv_color_hex(0xffffff), 0);
    lv_obj_move_foreground(cut);
}

static void ui_standby_create_battery_icon(lv_obj_t *parent)
{
    lv_obj_t *body = ui_standby_plain_obj(parent, 381, 637, 36, 22, 2, 3);
    lv_obj_t *cap = ui_standby_plain_obj(parent, 417, 644, 5, 8, 0, 1);
    lv_obj_t *level = ui_standby_plain_obj(parent, 386, 642, 24, 12, 0, 1);
    LV_UNUSED(body);
    LV_UNUSED(cap);
    LV_UNUSED(level);
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
    lv_obj_t *memo_box;

    if (ui_Standby != NULL)
    {
        return;
    }

    memset(&s_refs, 0, sizeof(s_refs));
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(s_cache_date_weather, 0, sizeof(s_cache_date_weather));
    memset(s_cache_room_temp, 0, sizeof(s_cache_room_temp));
    memset(s_cache_humidity, 0, sizeof(s_cache_humidity));
    memset(s_cache_battery, 0, sizeof(s_cache_battery));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    ui_Standby = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Standby, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Standby, LV_OPA_COVER, 0);

    s_refs.time_label = ui_create_label(ui_Standby, "20:36", 78, 47, 372, 147, 138, LV_TEXT_ALIGN_CENTER, false, false);
    ui_standby_style_label_color(s_refs.time_label);
    lv_label_set_long_mode(s_refs.time_label, LV_LABEL_LONG_CLIP);

    s_refs.date_weather_label = ui_create_label(ui_Standby,
                                                "05-25 星期四  晴 26℃",
                                                124,
                                                203,
                                                279,
                                                39,
                                                24,
                                                LV_TEXT_ALIGN_CENTER,
                                                false,
                                                false);
    ui_standby_style_label_color(s_refs.date_weather_label);
    lv_label_set_long_mode(s_refs.date_weather_label, LV_LABEL_LONG_DOT);

    ui_standby_create_alarm_icon(ui_Standby);
    s_refs.alarm_label = ui_create_label(ui_Standby, "08:00", 296, 333, 75, 32, 24, LV_TEXT_ALIGN_CENTER, false, false);
    ui_standby_style_label_color(s_refs.alarm_label);

    memo_box = ui_standby_plain_obj(ui_Standby, 31, 414, 464, 174, 1, 32);
    LV_UNUSED(memo_box);
    s_refs.memo_label = ui_create_label(ui_Standby,
                                        "备忘录：我是AI 小豆，很开心为您服务，您可以和我多多聊天互动哦～您可以和我多多聊天互动哦～",
                                        59,
                                        438,
                                        421,
                                        121,
                                        24,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    ui_standby_style_label_color(s_refs.memo_label);
    lv_label_set_long_mode(s_refs.memo_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_refs.memo_label, ui_px_y(10), 0);

    ui_standby_create_temp_icon(ui_Standby);
    ui_standby_create_humidity_icon(ui_Standby);
    ui_standby_create_battery_icon(ui_Standby);

    s_refs.room_temp_value_label = ui_create_label(ui_Standby, "25℃", 89, 676, 78, 38, 32, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.humidity_value_label = ui_create_label(ui_Standby, "49%", 226, 676, 82, 38, 32, LV_TEXT_ALIGN_CENTER, false, false);
    s_refs.battery_value_label = ui_create_label(ui_Standby, "78%", 360, 676, 88, 38, 32, LV_TEXT_ALIGN_CENTER, false, false);
    ui_standby_style_label_color(s_refs.room_temp_value_label);
    ui_standby_style_label_color(s_refs.humidity_value_label);
    ui_standby_style_label_color(s_refs.battery_value_label);

    ui_standby_style_label_color(ui_create_label(ui_Standby, "室温", 102, 718, 48, 32, 24, LV_TEXT_ALIGN_CENTER, false, false));
    ui_standby_style_label_color(ui_create_label(ui_Standby, "湿度", 239, 718, 48, 32, 24, LV_TEXT_ALIGN_CENTER, false, false));
    ui_standby_style_label_color(ui_create_label(ui_Standby, "电量", 379, 718, 48, 32, 24, LV_TEXT_ALIGN_CENTER, false, false));

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
    memset(s_cache_time, 0, sizeof(s_cache_time));
    memset(s_cache_date_weather, 0, sizeof(s_cache_date_weather));
    memset(s_cache_room_temp, 0, sizeof(s_cache_room_temp));
    memset(s_cache_humidity, 0, sizeof(s_cache_humidity));
    memset(s_cache_battery, 0, sizeof(s_cache_battery));
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));
    s_last_snapshot_valid = false;

    if (ui_Standby != NULL)
    {
        lv_obj_delete(ui_Standby);
        ui_Standby = NULL;
    }
}
