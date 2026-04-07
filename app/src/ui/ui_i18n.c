#include "ui_i18n.h"

#include <stddef.h>
#include <string.h>

#include "ui.h"

typedef struct
{
    const char *zh;
    const char *en;
} ui_i18n_mapping_t;

static const char *const s_weekdays_zh[] = {"一", "二", "三", "四", "五", "六", "日"};
static const char *const s_weekdays_en[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static const ui_i18n_mapping_t s_weekday_labels[] = {
    {"周一", "Mon"},
    {"星期一", "Mon"},
    {"一", "Mon"},
    {"周二", "Tue"},
    {"星期二", "Tue"},
    {"二", "Tue"},
    {"周三", "Wed"},
    {"星期三", "Wed"},
    {"三", "Wed"},
    {"周四", "Thu"},
    {"星期四", "Thu"},
    {"四", "Thu"},
    {"周五", "Fri"},
    {"星期五", "Fri"},
    {"五", "Fri"},
    {"周六", "Sat"},
    {"星期六", "Sat"},
    {"六", "Sat"},
    {"周日", "Sun"},
    {"星期日", "Sun"},
    {"星期天", "Sun"},
    {"日", "Sun"},
    {"天", "Sun"},
};

static const ui_i18n_mapping_t s_weather_labels[] = {
    {"晴", "Sunny"},
    {"多云", "Cloudy"},
    {"少云", "Partly Cloudy"},
    {"晴间多云", "Partly Sunny"},
    {"阴", "Overcast"},
    {"阵雨", "Shower"},
    {"雷阵雨", "Thunder Shower"},
    {"小雨", "Light Rain"},
    {"中雨", "Moderate Rain"},
    {"大雨", "Heavy Rain"},
    {"暴雨", "Storm Rain"},
    {"雨夹雪", "Sleet"},
    {"小雪", "Light Snow"},
    {"中雪", "Snow"},
    {"大雪", "Heavy Snow"},
    {"雾", "Fog"},
    {"霾", "Haze"},
    {"沙尘", "Dust"},
    {"未知", "Unknown"},
};

bool ui_i18n_is_english(void)
{
    return ui_settings_get_language() == UI_SETTINGS_LANGUAGE_EN_US;
}

const char *ui_i18n_pick(const char *zh_cn, const char *en_us)
{
    if (ui_i18n_is_english())
    {
        return en_us != NULL ? en_us : zh_cn;
    }

    return zh_cn != NULL ? zh_cn : en_us;
}

const char *ui_i18n_weekday_short(int monday_index)
{
    if (monday_index < 0 || monday_index >= 7)
    {
        return ui_i18n_weekday_fallback();
    }

    return ui_i18n_is_english() ? s_weekdays_en[monday_index] : s_weekdays_zh[monday_index];
}

const char *ui_i18n_weekday_fallback(void)
{
    return ui_i18n_pick("周?", "Day?");
}

static const char *ui_i18n_lookup(const ui_i18n_mapping_t *table,
                                  size_t count,
                                  const char *text)
{
    size_t i;

    if (text == NULL || text[0] == '\0')
    {
        return NULL;
    }

    for (i = 0; i < count; ++i)
    {
        if (strcmp(table[i].zh, text) == 0)
        {
            return table[i].en;
        }
    }

    return NULL;
}

const char *ui_i18n_translate_weekday_label(const char *text)
{
    const char *mapped;

    if (!ui_i18n_is_english())
    {
        return (text != NULL && text[0] != '\0') ? text : ui_i18n_weekday_fallback();
    }

    mapped = ui_i18n_lookup(s_weekday_labels,
                            sizeof(s_weekday_labels) / sizeof(s_weekday_labels[0]),
                            text);
    return mapped != NULL ? mapped : ui_i18n_weekday_fallback();
}

const char *ui_i18n_translate_weather_text(const char *text)
{
    const char *mapped;

    if (!ui_i18n_is_english())
    {
        return (text != NULL && text[0] != '\0') ? text : ui_i18n_pick("未知", "Unknown");
    }

    mapped = ui_i18n_lookup(s_weather_labels,
                            sizeof(s_weather_labels) / sizeof(s_weather_labels[0]),
                            text);
    return mapped != NULL ? mapped : ((text != NULL && text[0] != '\0') ? text : "Unknown");
}
