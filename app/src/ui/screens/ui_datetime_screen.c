#include "ui.h"
#include "ui_dispatch.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "../../xiaozhi/weather/weather.h"
#include "rtdevice.h"
#include "rtthread.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

lv_obj_t *ui_Datetime = NULL;

typedef enum
{
    UI_DATETIME_FIELD_YEAR = 0,
    UI_DATETIME_FIELD_MONTH,
    UI_DATETIME_FIELD_DAY,
    UI_DATETIME_FIELD_HOUR,
    UI_DATETIME_FIELD_MINUTE,
    UI_DATETIME_FIELD_COUNT,
} ui_datetime_field_t;

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *indicator;
    lv_obj_t *value_label;
} ui_datetime_row_refs_t;

static ui_datetime_row_refs_t s_datetime_rows[UI_DATETIME_FIELD_COUNT];
static lv_obj_t *s_datetime_current_label = NULL;
static lv_obj_t *s_datetime_status_label = NULL;
static lv_obj_t *s_datetime_hint_label = NULL;
static date_time_t s_datetime_edit = {0};
static ui_datetime_field_t s_datetime_selected_field = UI_DATETIME_FIELD_MINUTE;

static int ui_datetime_days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap_year;

    if (month < 1 || month > 12)
    {
        return 31;
    }

    if (month != 2)
    {
        return days[month - 1];
    }

    leap_year = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
    return leap_year ? 29 : 28;
}

static void ui_datetime_from_tm(date_time_t *value, const struct tm *tm_value)
{
    int weekday_index;

    if (value == NULL || tm_value == NULL)
    {
        return;
    }

    value->year = tm_value->tm_year + 1900;
    value->month = tm_value->tm_mon + 1;
    value->day = tm_value->tm_mday;
    value->hour = tm_value->tm_hour;
    value->minute = tm_value->tm_min;
    value->second = tm_value->tm_sec;
    value->weekday = tm_value->tm_wday;

    weekday_index = (tm_value->tm_wday + 6) % 7;
    rt_snprintf(value->weekday_str,
                sizeof(value->weekday_str),
                "%s",
                ui_i18n_weekday_short(weekday_index));
    rt_snprintf(value->date_str,
                sizeof(value->date_str),
                "%04d/%02d/%02d",
                value->year,
                value->month,
                value->day);
    rt_snprintf(value->time_str,
                sizeof(value->time_str),
                "%02d:%02d:%02d",
                value->hour,
                value->minute,
                value->second);
}

static void ui_datetime_normalize(date_time_t *value)
{
    struct tm tm_value = {0};
    time_t timestamp;

    if (value == NULL)
    {
        return;
    }

    if (value->year < 2020)
    {
        value->year = 2020;
    }
    if (value->year > 2099)
    {
        value->year = 2099;
    }
    if (value->month < 1)
    {
        value->month = 1;
    }
    if (value->month > 12)
    {
        value->month = 12;
    }
    if (value->day < 1)
    {
        value->day = 1;
    }
    if (value->day > ui_datetime_days_in_month(value->year, value->month))
    {
        value->day = ui_datetime_days_in_month(value->year, value->month);
    }
    if (value->hour < 0)
    {
        value->hour = 0;
    }
    if (value->hour > 23)
    {
        value->hour = 23;
    }
    if (value->minute < 0)
    {
        value->minute = 0;
    }
    if (value->minute > 59)
    {
        value->minute = 59;
    }

    tm_value.tm_year = value->year - 1900;
    tm_value.tm_mon = value->month - 1;
    tm_value.tm_mday = value->day;
    tm_value.tm_hour = value->hour;
    tm_value.tm_min = value->minute;
    tm_value.tm_sec = value->second;
    tm_value.tm_isdst = -1;

    timestamp = mktime(&tm_value);
    if (timestamp == (time_t)-1)
    {
        return;
    }

    ui_datetime_from_tm(value, &tm_value);
}

static void ui_datetime_load_current(void)
{
    if (xiaozhi_time_get_current(&s_datetime_edit) != RT_EOK ||
        s_datetime_edit.year < 2020 ||
        s_datetime_edit.month < 1 ||
        s_datetime_edit.month > 12 ||
        s_datetime_edit.day < 1 ||
        s_datetime_edit.day > 31)
    {
        s_datetime_edit.year = 2026;
        s_datetime_edit.month = 1;
        s_datetime_edit.day = 1;
        s_datetime_edit.hour = 1;
        s_datetime_edit.minute = 1;
        s_datetime_edit.second = 0;
    }

    s_datetime_edit.second = 0;
    ui_datetime_normalize(&s_datetime_edit);
}

static void ui_datetime_refresh_labels(void)
{
    static const ui_datetime_field_t fields[UI_DATETIME_FIELD_COUNT] = {
        UI_DATETIME_FIELD_YEAR,
        UI_DATETIME_FIELD_MONTH,
        UI_DATETIME_FIELD_DAY,
        UI_DATETIME_FIELD_HOUR,
        UI_DATETIME_FIELD_MINUTE,
    };
    uint32_t index;
    char value_text[16];
    char current_text[64];

    for (index = 0; index < UI_DATETIME_FIELD_COUNT; ++index)
    {
        if (s_datetime_rows[index].value_label == NULL)
        {
            continue;
        }

        switch (fields[index])
        {
        case UI_DATETIME_FIELD_YEAR:
            rt_snprintf(value_text, sizeof(value_text), "%04d", s_datetime_edit.year);
            break;
        case UI_DATETIME_FIELD_MONTH:
            rt_snprintf(value_text, sizeof(value_text), "%02d", s_datetime_edit.month);
            break;
        case UI_DATETIME_FIELD_DAY:
            rt_snprintf(value_text, sizeof(value_text), "%02d", s_datetime_edit.day);
            break;
        case UI_DATETIME_FIELD_HOUR:
            rt_snprintf(value_text, sizeof(value_text), "%02d", s_datetime_edit.hour);
            break;
        case UI_DATETIME_FIELD_MINUTE:
        default:
            rt_snprintf(value_text, sizeof(value_text), "%02d", s_datetime_edit.minute);
            break;
        }

        lv_label_set_text(s_datetime_rows[index].value_label, value_text);
    }

    if (s_datetime_current_label != NULL)
    {
        rt_snprintf(current_text,
                    sizeof(current_text),
                    "%04d / %02d / %02d  %02d : %02d  %s",
                    s_datetime_edit.year,
                    s_datetime_edit.month,
                    s_datetime_edit.day,
                    s_datetime_edit.hour,
                    s_datetime_edit.minute,
                    s_datetime_edit.weekday_str);
        lv_label_set_text(s_datetime_current_label, current_text);
    }
}

static void ui_datetime_update_selection_visuals(void)
{
    uint32_t index;

    for (index = 0; index < UI_DATETIME_FIELD_COUNT; ++index)
    {
        bool selected = (index == (uint32_t)s_datetime_selected_field);

        if (s_datetime_rows[index].indicator == NULL)
        {
            continue;
        }

        lv_obj_set_style_bg_opa(s_datetime_rows[index].indicator,
                                selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
        if (s_datetime_rows[index].card != NULL)
        {
            lv_obj_set_style_border_width(s_datetime_rows[index].card, selected ? 2 : 0, 0);
            lv_obj_set_style_border_color(s_datetime_rows[index].card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(s_datetime_rows[index].card, selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        }
    }

    if (s_datetime_hint_label != NULL)
    {
        lv_label_set_text(s_datetime_hint_label,
                          ui_i18n_pick("点击行切换项目，T/B 直接调整当前项",
                                       "Tap row to select, T/B adjusts"));
    }
}

static void ui_datetime_set_status(const char *text, bool success)
{
    if (s_datetime_status_label == NULL)
    {
        return;
    }

    lv_label_set_text(s_datetime_status_label, text);
    lv_obj_set_style_text_color(s_datetime_status_label,
                                success ? lv_color_hex(0x000000) : lv_color_hex(0x666666),
                                0);
}

static void ui_datetime_adjust_field(ui_datetime_field_t field, int delta)
{
    struct tm tm_value = {0};
    time_t timestamp;

    tm_value.tm_year = s_datetime_edit.year - 1900;
    tm_value.tm_mon = s_datetime_edit.month - 1;
    tm_value.tm_mday = s_datetime_edit.day;
    tm_value.tm_hour = s_datetime_edit.hour;
    tm_value.tm_min = s_datetime_edit.minute;
    tm_value.tm_sec = 0;
    tm_value.tm_isdst = -1;

    switch (field)
    {
    case UI_DATETIME_FIELD_YEAR:
        tm_value.tm_year += delta;
        break;
    case UI_DATETIME_FIELD_MONTH:
        tm_value.tm_mon += delta;
        break;
    case UI_DATETIME_FIELD_DAY:
        tm_value.tm_mday += delta;
        break;
    case UI_DATETIME_FIELD_HOUR:
        tm_value.tm_hour += delta;
        break;
    case UI_DATETIME_FIELD_MINUTE:
    default:
        tm_value.tm_min += delta;
        break;
    }

    timestamp = mktime(&tm_value);
    if (timestamp == (time_t)-1)
    {
        ui_datetime_set_status(ui_i18n_pick("时间调整失败", "Adjust failed"), false);
        return;
    }

    ui_datetime_from_tm(&s_datetime_edit, &tm_value);
    ui_datetime_normalize(&s_datetime_edit);
    ui_datetime_refresh_labels();
    ui_datetime_set_status(ui_i18n_pick("修改后点击保存时间", "Tap save to apply"), true);
}

static void ui_datetime_row_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    s_datetime_selected_field = (ui_datetime_field_t)(uintptr_t)lv_event_get_user_data(e);
    ui_datetime_update_selection_visuals();
}

static void ui_datetime_adjust_event_cb(lv_event_t *e)
{
    uintptr_t encoded;
    ui_datetime_field_t field;
    int delta;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    encoded = (uintptr_t)lv_event_get_user_data(e);
    field = (ui_datetime_field_t)(encoded >> 1);
    delta = (encoded & 0x1U) ? 1 : -1;
    ui_datetime_adjust_field(field, delta);
    s_datetime_selected_field = field;
    ui_datetime_update_selection_visuals();
}

static void ui_datetime_save_event_cb(lv_event_t *e)
{
    struct tm tm_value = {0};
    time_t timestamp;
    rt_device_t rtc_device;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    tm_value.tm_year = s_datetime_edit.year - 1900;
    tm_value.tm_mon = s_datetime_edit.month - 1;
    tm_value.tm_mday = s_datetime_edit.day;
    tm_value.tm_hour = s_datetime_edit.hour;
    tm_value.tm_min = s_datetime_edit.minute;
    tm_value.tm_sec = 0;
    tm_value.tm_isdst = -1;

    timestamp = mktime(&tm_value);
    if (timestamp == (time_t)-1)
    {
        ui_datetime_set_status(ui_i18n_pick("保存失败", "Save failed"), false);
        return;
    }

    rtc_device = rt_device_find("rtc");
    if (rtc_device == RT_NULL)
    {
        ui_datetime_set_status(ui_i18n_pick("未找到 RTC", "RTC not found"), false);
        return;
    }

    if (rt_device_control(rtc_device, RT_DEVICE_CTRL_RTC_SET_TIME, &timestamp) != RT_EOK)
    {
        ui_datetime_set_status(ui_i18n_pick("RTC 写入失败", "RTC write failed"), false);
        return;
    }

    ui_datetime_from_tm(&s_datetime_edit, &tm_value);
    ui_datetime_refresh_labels();
    ui_datetime_set_status(ui_i18n_pick("时间已保存", "Time saved"), true);
    ui_dispatch_request_time_refresh();
    ui_dispatch_request_back();
}

static void ui_datetime_reset_default_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    s_datetime_edit.year = 2026;
    s_datetime_edit.month = 1;
    s_datetime_edit.day = 1;
    s_datetime_edit.hour = 1;
    s_datetime_edit.minute = 1;
    s_datetime_edit.second = 0;
    ui_datetime_normalize(&s_datetime_edit);
    s_datetime_selected_field = UI_DATETIME_FIELD_MINUTE;
    ui_datetime_refresh_labels();
    ui_datetime_update_selection_visuals();
    ui_datetime_set_status(ui_i18n_pick("已恢复默认时间，点击保存生效", "Default restored, tap save"), true);
}

static lv_obj_t *ui_datetime_create_adjust_button(lv_obj_t *parent,
                                                  int x,
                                                  uintptr_t encoded,
                                                  const char *text)
{
    lv_obj_t *button = ui_create_button(parent, x, 18, 56, 48, text, 28, UI_SCREEN_NONE, false);

    lv_obj_add_event_cb(button, ui_datetime_adjust_event_cb, LV_EVENT_CLICKED, (void *)encoded);
    return button;
}

static void ui_datetime_create_row(lv_obj_t *parent,
                                   int y,
                                   ui_datetime_field_t field,
                                   const char *name)
{
    lv_obj_t *row;
    lv_obj_t *divider;

    row = ui_create_card(parent, 0, y, 528, 84, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_event_cb(row, ui_datetime_row_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)field);

    s_datetime_rows[field].card = row;
    s_datetime_rows[field].indicator = lv_obj_create(row);
    lv_obj_remove_flag(s_datetime_rows[field].indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_datetime_rows[field].indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_datetime_rows[field].indicator, 0, 0);
    lv_obj_set_style_bg_color(s_datetime_rows[field].indicator, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_datetime_rows[field].indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_datetime_rows[field].indicator, 0, 0);
    lv_obj_set_size(s_datetime_rows[field].indicator, 8, 84);
    lv_obj_set_pos(s_datetime_rows[field].indicator, 0, 0);

    ui_create_label(row, name, 24, 26, 96, 28, 24, LV_TEXT_ALIGN_LEFT, false, false);
    ui_datetime_create_adjust_button(row, 196, ((uintptr_t)field << 1), "-");
    s_datetime_rows[field].value_label = ui_create_label(row,
                                                         "--",
                                                         270,
                                                         24,
                                                         80,
                                                         32,
                                                         28,
                                                         LV_TEXT_ALIGN_CENTER,
                                                         false,
                                                         false);
    ui_datetime_create_adjust_button(row, 380, (((uintptr_t)field << 1) | 0x1U), "+");

    divider = lv_obj_create(row);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_size(divider, 528, 2);
    lv_obj_set_pos(divider, 0, 82);
}

void ui_Datetime_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *reset_button;
    lv_obj_t *save_button;

    if (ui_Datetime != NULL)
    {
        return;
    }

    ui_datetime_load_current();

    ui_Datetime = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Datetime, ui_i18n_pick("日期与时间", "Date & Time"), UI_SCREEN_TIME_MANAGE);

    ui_create_label(page.content,
                    ui_i18n_pick("当前设备时间", "Current Device Time"),
                    20,
                    18,
                    488,
                    24,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    s_datetime_current_label = ui_create_label(page.content,
                                               "",
                                               20,
                                               48,
                                               488,
                                                34,
                                                28,
                                               LV_TEXT_ALIGN_CENTER,
                                               false,
                                               false);

    s_datetime_status_label = ui_create_label(page.content,
                                              ui_i18n_pick("修改后点击保存时间", "Tap save to apply"),
                                              20,
                                              88,
                                              488,
                                              22,
                                              18,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);
    lv_obj_set_style_text_color(s_datetime_status_label, lv_color_hex(0x666666), 0);

    s_datetime_hint_label = ui_create_label(page.content,
                                            ui_i18n_pick("点击行切换项目，T/B 直接调整当前项",
                                                         "Tap row to select, T/B adjusts"),
                                            20,
                                            112,
                                            488,
                                            22,
                                            18,
                                            LV_TEXT_ALIGN_CENTER,
                                            false,
                                            false);
    lv_obj_set_style_text_color(s_datetime_hint_label, lv_color_hex(0x666666), 0);

    ui_datetime_create_row(page.content, 146, UI_DATETIME_FIELD_YEAR, ui_i18n_pick("年", "Year"));
    ui_datetime_create_row(page.content, 230, UI_DATETIME_FIELD_MONTH, ui_i18n_pick("月", "Month"));
    ui_datetime_create_row(page.content, 314, UI_DATETIME_FIELD_DAY, ui_i18n_pick("日", "Day"));
    ui_datetime_create_row(page.content, 398, UI_DATETIME_FIELD_HOUR, ui_i18n_pick("时", "Hour"));
    ui_datetime_create_row(page.content, 482, UI_DATETIME_FIELD_MINUTE, ui_i18n_pick("分", "Minute"));

    reset_button = ui_create_button(page.content,
                                    86,
                                    576,
                                    156,
                                    48,
                                    ui_i18n_pick("恢复默认", "Reset"),
                                    22,
                                    UI_SCREEN_NONE,
                                    false);
    lv_obj_add_event_cb(reset_button, ui_datetime_reset_default_event_cb, LV_EVENT_CLICKED, NULL);

    save_button = ui_create_button(page.content,
                                   286,
                                   576,
                                   156,
                                   48,
                                   ui_i18n_pick("保存时间", "Save Time"),
                                   22,
                                   UI_SCREEN_NONE,
                                   true);
    lv_obj_add_event_cb(save_button, ui_datetime_save_event_cb, LV_EVENT_CLICKED, NULL);

    ui_datetime_refresh_labels();
    ui_datetime_update_selection_visuals();
}

void ui_Datetime_screen_destroy(void)
{
    if (ui_Datetime != NULL)
    {
        lv_obj_delete(ui_Datetime);
        ui_Datetime = NULL;
    }

    memset(s_datetime_rows, 0, sizeof(s_datetime_rows));
    s_datetime_current_label = NULL;
    s_datetime_status_label = NULL;
    s_datetime_hint_label = NULL;
}

void ui_datetime_hardware_adjust(int direction)
{
    if (direction == 0)
    {
        return;
    }

    ui_datetime_adjust_field(s_datetime_selected_field, direction < 0 ? -1 : 1);
    ui_datetime_update_selection_visuals();
}
