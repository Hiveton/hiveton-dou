#ifndef APP_UI_EPD_REFRESH_POLICY_H
#define APP_UI_EPD_REFRESH_POLICY_H

#include <stdbool.h>

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UI_EPD_PROFILE_MONO_UI = 0,
    UI_EPD_PROFILE_TEXT,
    UI_EPD_PROFILE_COVER_GRID,
    UI_EPD_PROFILE_GRAY_IMAGE,
    UI_EPD_PROFILE_WALLPAPER,
    UI_EPD_PROFILE_STANDBY,
} ui_epd_refresh_profile_t;

ui_epd_refresh_profile_t ui_epd_refresh_policy_screen_profile(ui_screen_id_t screen_id);
bool ui_epd_refresh_policy_should_full_after_gray(ui_screen_id_t from, ui_screen_id_t to);
void ui_epd_refresh_policy_request_clean_refresh(const char *reason);
void ui_epd_refresh_policy_request_image_refresh(const char *reason);

#ifdef __cplusplus
}
#endif

#endif
