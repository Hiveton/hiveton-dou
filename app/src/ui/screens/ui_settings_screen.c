#include "ui.h"
#include "ui_helpers.h"
#include "rtthread.h"
#include "../../xiaozhi/weather/weather.h"

lv_obj_t *ui_Settings = NULL;

static int s_sleep_start_minutes = 22 * 60 + 30;
static ui_settings_language_t s_language = UI_SETTINGS_LANGUAGE_ZH_CN;

extern void app_set_panel_brightness(rt_uint8_t brightness);
extern rt_uint8_t app_get_panel_brightness(void);

static const char *ui_settings_title_text(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Settings";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "设置";
    }
}

static const char *ui_settings_sleep_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Sleep Timer";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "助眠时间";
    }
}

static const char *ui_settings_weather_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Weather Entry";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "天气开关";
    }
}

static const char *ui_settings_brightness_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Brightness";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "屏幕亮度";
    }
}

static const char *ui_settings_language_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Language";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "语言";
    }
}

static const char *ui_settings_weather_summary_enabled(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Weather card is shown on Home";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "首页天气卡片已开启";
    }
}

static const char *ui_settings_weather_summary_disabled(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Weather card is hidden on Home";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "首页天气卡片已关闭";
    }
}

static int ui_settings_normalize_minutes(int minutes)
{
    const int day_minutes = 24 * 60;

    while (minutes < 0)
    {
        minutes += day_minutes;
    }

    while (minutes >= day_minutes)
    {
        minutes -= day_minutes;
    }

    return minutes;
}

static void ui_settings_format_time(int minutes, char *buffer, size_t buffer_size)
{
    int normalized = ui_settings_normalize_minutes(minutes);
    int hour = normalized / 60;
    int minute = normalized % 60;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    rt_snprintf(buffer, buffer_size, "%02d:%02d", hour, minute);
}

static void ui_settings_format_sleep_summary(char *buffer, size_t buffer_size)
{
    char time_text[16];

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    ui_settings_format_time(s_sleep_start_minutes, time_text, sizeof(time_text));
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        rt_snprintf(buffer, buffer_size, "Quiet reminder starts at %s", time_text);
        break;
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        rt_snprintf(buffer, buffer_size, "%s 开始柔和提醒", time_text);
        break;
    }
}

static void ui_settings_format_brightness_summary(char *buffer, size_t buffer_size)
{
    rt_uint8_t brightness;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    brightness = app_get_panel_brightness();
    if (brightness == 0U)
    {
        switch (s_language)
        {
        case UI_SETTINGS_LANGUAGE_EN_US:
            rt_snprintf(buffer, buffer_size, "Currently off");
            break;
        case UI_SETTINGS_LANGUAGE_ZH_CN:
        default:
            rt_snprintf(buffer, buffer_size, "当前已关闭");
            break;
        }
    }
    else
    {
        switch (s_language)
        {
        case UI_SETTINGS_LANGUAGE_EN_US:
            rt_snprintf(buffer, buffer_size, "Current brightness %u%%", (unsigned int)brightness);
            break;
        case UI_SETTINGS_LANGUAGE_ZH_CN:
        default:
            rt_snprintf(buffer, buffer_size, "当前亮度 %u%%", (unsigned int)brightness);
            break;
        }
    }
}

static void create_settings_card(lv_obj_t *parent,
                                 int y,
                                 const char *title,
                                 const char *subtitle,
                                 ui_screen_id_t target)
{
    lv_obj_t *card = ui_create_card(parent, 24, y, 480, 112, target, false, 0);

    ui_create_label(card,
                    title,
                    24,
                    34,
                    432,
                    34,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    subtitle,
                    24,
                    79,
                    432,
                    21,
                    17,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
}

ui_settings_language_t ui_settings_get_language(void)
{
    return s_language;
}

void ui_settings_set_language(ui_settings_language_t language)
{
    if (language >= UI_SETTINGS_LANGUAGE_COUNT)
    {
        return;
    }

    s_language = language;
}

const char *ui_settings_get_language_label(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "English";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "简体中文";
    }
}

int ui_settings_get_sleep_start_minutes(void)
{
    return s_sleep_start_minutes;
}

void ui_settings_set_sleep_start_minutes(int minutes)
{
    s_sleep_start_minutes = ui_settings_normalize_minutes(minutes);
}

void ui_settings_adjust_sleep_start_minutes(int delta_minutes)
{
    ui_settings_set_sleep_start_minutes(s_sleep_start_minutes + delta_minutes);
}

void ui_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;
    char sleep_summary[48];
    char brightness_summary[32];

    if (ui_Settings != NULL)
    {
        return;
    }

    ui_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Settings, ui_settings_title_text(), UI_SCREEN_HOME);

    ui_settings_format_sleep_summary(sleep_summary, sizeof(sleep_summary));
    ui_settings_format_brightness_summary(brightness_summary, sizeof(brightness_summary));

    create_settings_card(page.content, 56, ui_settings_sleep_card_title(), sleep_summary, UI_SCREEN_SLEEP_TIME);
    create_settings_card(page.content,
                         188,
                         ui_settings_weather_card_title(),
                         xiaozhi_weather_is_home_entry_enabled() ? ui_settings_weather_summary_enabled() : ui_settings_weather_summary_disabled(),
                         UI_SCREEN_WEATHER_TOGGLE);
    create_settings_card(page.content, 320, ui_settings_brightness_card_title(), brightness_summary, UI_SCREEN_BRIGHTNESS);
    create_settings_card(page.content, 452, ui_settings_language_card_title(), ui_settings_get_language_label(), UI_SCREEN_LANGUAGE);
}

void ui_Settings_screen_destroy(void)
{
    if (ui_Settings != NULL)
    {
        lv_obj_delete(ui_Settings);
        ui_Settings = NULL;
    }
}
