#include "ui_epd_refresh_policy.h"

#include "drv_lcd.h"
#include "rtthread.h"

ui_epd_refresh_profile_t ui_epd_refresh_policy_screen_profile(ui_screen_id_t screen_id)
{
    switch (screen_id)
    {
    case UI_SCREEN_STANDBY:
        return UI_EPD_PROFILE_STANDBY;
    case UI_SCREEN_WALLPAPER:
        return UI_EPD_PROFILE_WALLPAPER;
    case UI_SCREEN_READING_DETAIL:
        return UI_EPD_PROFILE_TEXT;
    case UI_SCREEN_READING_LIST:
        return UI_EPD_PROFILE_COVER_GRID;
    case UI_SCREEN_HOME:
    case UI_SCREEN_PET:
    case UI_SCREEN_PET_RULES:
    case UI_SCREEN_AI_DOU:
    case UI_SCREEN_POMODORO:
    case UI_SCREEN_DATETIME:
    case UI_SCREEN_WEATHER:
    case UI_SCREEN_CALENDAR:
    case UI_SCREEN_STATUS_DETAIL:
    case UI_SCREEN_ABOUT:
    case UI_SCREEN_RECORDER:
    case UI_SCREEN_RECORD_LIST:
    case UI_SCREEN_MUSIC_LIST:
    case UI_SCREEN_MUSIC_PLAYER:
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_BRIGHTNESS:
    case UI_SCREEN_LANGUAGE:
    case UI_SCREEN_AI_WEATHER_SETTINGS:
    case UI_SCREEN_MORE:
    case UI_SCREEN_CALCULATOR:
    case UI_SCREEN_READING_TOC:
    case UI_SCREEN_READING_FONT:
    case UI_SCREEN_NONE:
    case UI_SCREEN_COUNT:
    default:
        return UI_EPD_PROFILE_MONO_UI;
    }
}

static bool ui_epd_refresh_policy_profile_needs_gray_cleanup(ui_epd_refresh_profile_t profile)
{
    switch (profile)
    {
    case UI_EPD_PROFILE_COVER_GRID:
    case UI_EPD_PROFILE_GRAY_IMAGE:
    case UI_EPD_PROFILE_WALLPAPER:
    case UI_EPD_PROFILE_STANDBY:
        return true;
    case UI_EPD_PROFILE_TEXT:
    case UI_EPD_PROFILE_MONO_UI:
    default:
        return false;
    }
}

bool ui_epd_refresh_policy_should_full_after_gray(ui_screen_id_t from, ui_screen_id_t to)
{
    ui_epd_refresh_profile_t from_profile;
    ui_epd_refresh_profile_t to_profile;

    if (to == UI_SCREEN_NONE)
    {
        return false;
    }

    if (!lcd_epd_last_displayed_frame_has_gray())
    {
        return false;
    }

    from_profile = ui_epd_refresh_policy_screen_profile(from);
    to_profile = ui_epd_refresh_policy_screen_profile(to);

    if (from_profile == UI_EPD_PROFILE_MONO_UI &&
        to_profile == UI_EPD_PROFILE_MONO_UI)
    {
        return false;
    }

    return ui_epd_refresh_policy_profile_needs_gray_cleanup(from_profile) ||
           ui_epd_refresh_policy_profile_needs_gray_cleanup(to_profile);
}

void ui_epd_refresh_policy_request_clean_refresh(const char *reason)
{
    (void)reason;
    lcd_request_epd_force_full_refresh_once();
}

void ui_epd_refresh_policy_request_image_refresh(const char *reason)
{
    (void)reason;
    lcd_request_epd_force_full_refresh_once();
    lcd_set_epd_image_refresh_hint(RT_TRUE);
}
