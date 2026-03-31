#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "rtthread.h"
#include "ui.h"
#include "ui_helpers.h"

#define UI_CALENDAR_CELL_COUNT 42U
#define UI_CALENDAR_REFRESH_MS 30000U

typedef struct
{
    int year;
    int month;
    int day;
} ui_calendar_date_t;

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *day_label;
} ui_calendar_cell_refs_t;

typedef struct
{
    lv_obj_t *title_label;
    lv_obj_t *meta_label;
    lv_obj_t *summary_label;
    lv_obj_t *detail_label;
    lv_obj_t *today_button;
    lv_timer_t *refresh_timer;
    ui_calendar_cell_refs_t cells[UI_CALENDAR_CELL_COUNT];
} ui_calendar_refs_t;

lv_obj_t *ui_Calendar = NULL;

static const char *s_week_labels[] = {"一", "二", "三", "四", "五", "六", "日"};
static ui_calendar_refs_t s_calendar_refs;
static ui_calendar_date_t s_calendar_today;
static ui_calendar_date_t s_calendar_selected;
static int s_calendar_view_year = 2026;
static int s_calendar_view_month = 1;

static bool ui_calendar_dates_equal(const ui_calendar_date_t *lhs, const ui_calendar_date_t *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return false;
    }

    return lhs->year == rhs->year &&
           lhs->month == rhs->month &&
           lhs->day == rhs->day;
}

static void ui_calendar_normalize_year_month(int *year, int *month)
{
    if (year == NULL || month == NULL)
    {
        return;
    }

    while (*month < 1)
    {
        *month += 12;
        --(*year);
    }

    while (*month > 12)
    {
        *month -= 12;
        ++(*year);
    }
}

static bool ui_calendar_is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int ui_calendar_days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2)
    {
        return ui_calendar_is_leap_year(year) ? 29 : 28;
    }

    if (month < 1 || month > 12)
    {
        return 30;
    }

    return days[month - 1];
}

static int ui_calendar_weekday_monday0(int year, int month, int day)
{
    struct tm tm_value;

    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = 12;
    (void)mktime(&tm_value);

    return (tm_value.tm_wday + 6) % 7;
}

static void ui_calendar_get_current_date(ui_calendar_date_t *date)
{
    time_t now;
    struct tm local_tm;

    if (date == NULL)
    {
        return;
    }

    now = time(NULL);
    if (now <= 0 || localtime_r(&now, &local_tm) == NULL)
    {
        date->year = 2026;
        date->month = 1;
        date->day = 1;
        return;
    }

    date->year = local_tm.tm_year + 1900;
    date->month = local_tm.tm_mon + 1;
    date->day = local_tm.tm_mday;
}

static const char *ui_calendar_get_week_label(int year, int month, int day)
{
    int week_index = ui_calendar_weekday_monday0(year, month, day);

    if (week_index < 0 || week_index >= 7)
    {
        return s_week_labels[0];
    }

    return s_week_labels[week_index];
}

static void ui_calendar_refresh_summary(void)
{
    char summary[48];
    char detail[96];
    int days_in_month;

    if (s_calendar_refs.summary_label == NULL || s_calendar_refs.detail_label == NULL)
    {
        return;
    }

    days_in_month = ui_calendar_days_in_month(s_calendar_selected.year, s_calendar_selected.month);
    rt_snprintf(summary,
                sizeof(summary),
                "%04d 年 %02d 月 %02d 日",
                s_calendar_selected.year,
                s_calendar_selected.month,
                s_calendar_selected.day);

    rt_snprintf(detail,
                sizeof(detail),
                "%s · %s · 本月 %d 天",
                ui_calendar_dates_equal(&s_calendar_selected, &s_calendar_today) ? "今天" : "已选日期",
                ui_calendar_get_week_label(s_calendar_selected.year, s_calendar_selected.month, s_calendar_selected.day),
                days_in_month);

    lv_label_set_text(s_calendar_refs.summary_label, summary);
    lv_label_set_text(s_calendar_refs.detail_label, detail);
}

static void ui_calendar_render(void)
{
    int first_weekday;
    int days_in_month;
    int prev_year;
    int prev_month;
    int next_year;
    int next_month;
    int prev_month_days;
    int cell_index;
    char title[32];
    char meta[48];

    if (ui_Calendar == NULL)
    {
        return;
    }

    first_weekday = ui_calendar_weekday_monday0(s_calendar_view_year, s_calendar_view_month, 1);
    days_in_month = ui_calendar_days_in_month(s_calendar_view_year, s_calendar_view_month);

    prev_year = s_calendar_view_year;
    prev_month = s_calendar_view_month - 1;
    ui_calendar_normalize_year_month(&prev_year, &prev_month);
    prev_month_days = ui_calendar_days_in_month(prev_year, prev_month);

    next_year = s_calendar_view_year;
    next_month = s_calendar_view_month + 1;
    ui_calendar_normalize_year_month(&next_year, &next_month);

    rt_snprintf(title, sizeof(title), "%04d 年 %d 月", s_calendar_view_year, s_calendar_view_month);
    rt_snprintf(meta,
                sizeof(meta),
                "今天 %04d-%02d-%02d · 星期%s",
                s_calendar_today.year,
                s_calendar_today.month,
                s_calendar_today.day,
                ui_calendar_get_week_label(s_calendar_today.year, s_calendar_today.month, s_calendar_today.day));

    lv_label_set_text(s_calendar_refs.title_label, title);
    lv_label_set_text(s_calendar_refs.meta_label, meta);

    for (cell_index = 0; cell_index < (int)UI_CALENDAR_CELL_COUNT; ++cell_index)
    {
        ui_calendar_cell_refs_t *cell = &s_calendar_refs.cells[cell_index];
        ui_calendar_date_t date;
        bool in_current_month;
        bool is_selected;
        bool is_today;
        char day_text[8];

        if (cell_index < first_weekday)
        {
            date.year = prev_year;
            date.month = prev_month;
            date.day = prev_month_days - first_weekday + cell_index + 1;
            in_current_month = false;
        }
        else if (cell_index >= first_weekday + days_in_month)
        {
            date.year = next_year;
            date.month = next_month;
            date.day = cell_index - (first_weekday + days_in_month) + 1;
            in_current_month = false;
        }
        else
        {
            date.year = s_calendar_view_year;
            date.month = s_calendar_view_month;
            date.day = cell_index - first_weekday + 1;
            in_current_month = true;
        }

        is_selected = ui_calendar_dates_equal(&date, &s_calendar_selected);
        is_today = ui_calendar_dates_equal(&date, &s_calendar_today);

        rt_snprintf(day_text, sizeof(day_text), "%d", date.day);
        lv_label_set_text(cell->day_label, day_text);
        lv_obj_center(cell->day_label);

        if (is_selected)
        {
            lv_obj_set_style_bg_opa(cell->card, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell->card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_border_width(cell->card, 2, 0);
            lv_obj_set_style_border_color(cell->card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_color(cell->day_label, lv_color_hex(0xFFFFFF), 0);
        }
        else
        {
            lv_obj_set_style_bg_opa(cell->card, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(cell->card, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(cell->card, is_today ? 2 : 1, 0);
            lv_obj_set_style_border_color(cell->card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_color(cell->day_label,
                                        in_current_month ? lv_color_hex(0x000000) : lv_color_hex(0x8C8C8C),
                                        0);
        }
    }

    ui_calendar_refresh_summary();
}

static void ui_calendar_select_date(int year, int month, int day)
{
    s_calendar_selected.year = year;
    s_calendar_selected.month = month;
    s_calendar_selected.day = day;
    s_calendar_view_year = year;
    s_calendar_view_month = month;
    ui_calendar_render();
}

static void ui_calendar_change_month(int delta)
{
    s_calendar_view_month += delta;
    ui_calendar_normalize_year_month(&s_calendar_view_year, &s_calendar_view_month);
    ui_calendar_render();
}

static void ui_calendar_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_calendar_change_month(-1);
    }
}

static void ui_calendar_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_calendar_change_month(1);
    }
}

static void ui_calendar_today_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_calendar_get_current_date(&s_calendar_today);
        ui_calendar_select_date(s_calendar_today.year, s_calendar_today.month, s_calendar_today.day);
    }
}

static void ui_calendar_cell_event_cb(lv_event_t *e)
{
    uintptr_t raw_index;
    int first_weekday;
    int days_in_month;
    int year;
    int month;
    int day;
    int index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    raw_index = (uintptr_t)lv_event_get_user_data(e);
    index = (int)raw_index;
    first_weekday = ui_calendar_weekday_monday0(s_calendar_view_year, s_calendar_view_month, 1);
    days_in_month = ui_calendar_days_in_month(s_calendar_view_year, s_calendar_view_month);
    year = s_calendar_view_year;
    month = s_calendar_view_month;

    if (index < first_weekday)
    {
        month -= 1;
        ui_calendar_normalize_year_month(&year, &month);
        day = ui_calendar_days_in_month(year, month) - first_weekday + index + 1;
    }
    else if (index >= first_weekday + days_in_month)
    {
        month += 1;
        ui_calendar_normalize_year_month(&year, &month);
        day = index - (first_weekday + days_in_month) + 1;
    }
    else
    {
        day = index - first_weekday + 1;
    }

    ui_calendar_select_date(year, month, day);
}

static void ui_calendar_refresh_timer_cb(lv_timer_t *timer)
{
    ui_calendar_date_t latest_today;

    LV_UNUSED(timer);

    if (ui_Calendar == NULL)
    {
        return;
    }

    ui_calendar_get_current_date(&latest_today);
    if (!ui_calendar_dates_equal(&latest_today, &s_calendar_today))
    {
        s_calendar_today = latest_today;
        ui_calendar_render();
    }
}

void ui_Calendar_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *summary_card;
    uint16_t row;
    uint16_t col;

    if (ui_Calendar != NULL)
    {
        return;
    }

    memset(&s_calendar_refs, 0, sizeof(s_calendar_refs));
    ui_calendar_get_current_date(&s_calendar_today);
    s_calendar_selected = s_calendar_today;
    s_calendar_view_year = s_calendar_today.year;
    s_calendar_view_month = s_calendar_today.month;

    ui_Calendar = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Calendar, "日历", UI_SCREEN_HOME);

    s_calendar_refs.title_label = ui_create_label(page.content,
                                                  "",
                                                  84,
                                                  12,
                                                  300,
                                                  28,
                                                  24,
                                                  LV_TEXT_ALIGN_CENTER,
                                                  false,
                                                  false);
    s_calendar_refs.meta_label = ui_create_label(page.content,
                                                 "",
                                                 84,
                                                 42,
                                                 300,
                                                 18,
                                                 15,
                                                 LV_TEXT_ALIGN_CENTER,
                                                 false,
                                                 false);

    s_calendar_refs.today_button = ui_create_button(page.content, 394, 18, 108, 36, "今天", 26, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_calendar_refs.today_button, ui_calendar_today_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t *prev_button = ui_create_nav_button(page.content, 24, 14, 42, 42, "<", UI_SCREEN_NONE);
        lv_obj_t *next_button = ui_create_nav_button(page.content, 516, 14, 42, 42, ">", UI_SCREEN_NONE);
        lv_obj_add_event_cb(prev_button, ui_calendar_prev_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(next_button, ui_calendar_next_event_cb, LV_EVENT_CLICKED, NULL);
    }

    for (col = 0; col < 7U; ++col)
    {
        ui_create_label(page.content,
                        s_week_labels[col],
                        24 + (int)col * 76,
                        80,
                        76,
                        24,
                        26,
                        LV_TEXT_ALIGN_CENTER,
                        false,
                        false);
    }

    for (row = 0; row < 6U; ++row)
    {
        for (col = 0; col < 7U; ++col)
        {
            uint16_t index = (uint16_t)(row * 7U + col);
            ui_calendar_cell_refs_t *cell = &s_calendar_refs.cells[index];

            cell->card = ui_create_card(page.content,
                                        24 + (int)col * 76,
                                        112 + (int)row * 68,
                                        73,
                                        63,
                                        UI_SCREEN_NONE,
                                        false,
                                        0);
            lv_obj_set_style_radius(cell->card, 0, 0);
            lv_obj_add_flag(cell->card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell->card,
                                ui_calendar_cell_event_cb,
                                LV_EVENT_CLICKED,
                                (void *)(uintptr_t)index);

            cell->day_label = ui_create_label(cell->card,
                                              "",
                                              0,
                                              0,
                                              0,
                                              0,
                                              26,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);
        }
    }

    summary_card = ui_create_card(page.content, 24, 534, 534, 78, UI_SCREEN_NONE, false, 0);
    s_calendar_refs.summary_label = ui_create_label(summary_card,
                                                    "",
                                                    18,
                                                    14,
                                                    320,
                                                    24,
                                                    20,
                                                    LV_TEXT_ALIGN_LEFT,
                                                    false,
                                                    false);
    s_calendar_refs.detail_label = ui_create_label(summary_card,
                                                   "",
                                                   18,
                                                   44,
                                                   320,
                                                   18,
                                                   15,
                                                   LV_TEXT_ALIGN_LEFT,
                                                   false,
                                                   false);

    ui_calendar_render();

    if (s_calendar_refs.refresh_timer == NULL)
    {
        s_calendar_refs.refresh_timer = lv_timer_create(ui_calendar_refresh_timer_cb,
                                                        UI_CALENDAR_REFRESH_MS,
                                                        NULL);
    }
}

void ui_Calendar_screen_destroy(void)
{
    if (s_calendar_refs.refresh_timer != NULL)
    {
        lv_timer_delete(s_calendar_refs.refresh_timer);
        s_calendar_refs.refresh_timer = NULL;
    }

    if (ui_Calendar != NULL)
    {
        lv_obj_delete(ui_Calendar);
        ui_Calendar = NULL;
    }

    memset(&s_calendar_refs, 0, sizeof(s_calendar_refs));
    memset(&s_calendar_today, 0, sizeof(s_calendar_today));
    memset(&s_calendar_selected, 0, sizeof(s_calendar_selected));
    s_calendar_view_year = 2026;
    s_calendar_view_month = 1;
}
