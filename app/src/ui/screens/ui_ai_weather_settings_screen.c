#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "config/app_config.h"

#include <stdbool.h>

lv_obj_t *ui_AI_Weather_Settings = NULL;

static lv_obj_t *s_ai_weather_ai_label = NULL;
static lv_obj_t *s_ai_weather_weather_label = NULL;
static lv_obj_t *s_ai_weather_ai_on = NULL;
static lv_obj_t *s_ai_weather_ai_off = NULL;
static lv_obj_t *s_ai_weather_weather_on = NULL;
static lv_obj_t *s_ai_weather_weather_off = NULL;

typedef enum
{
    AI_WEATHER_TOGGLE_AI_AUTO_RESUME = 0,
    AI_WEATHER_TOGGLE_WEATHER_REFRESH = 1,
} ai_weather_toggle_kind_t;

static const char *ui_ai_weather_title_text(void)
{
    return ui_i18n_pick("AI与天气", "AI & Weather");
}

static const char *ui_ai_weather_toggle_on_text(void)
{
    return ui_i18n_pick("开启", "On");
}

static const char *ui_ai_weather_toggle_off_text(void)
{
    return ui_i18n_pick("关闭", "Off");
}

static const char *ui_ai_weather_ai_title(void)
{
    return ui_i18n_pick("AI自动重连", "AI auto reconnect");
}

static const char *ui_ai_weather_weather_title(void)
{
    return ui_i18n_pick("天气自动刷新", "Weather auto refresh");
}

static const char *ui_ai_weather_ai_summary(bool enabled)
{
    if (enabled)
    {
        return ui_i18n_pick("网络恢复后自动重连小智", "Reconnect Xiaozhi when network recovers");
    }

    return ui_i18n_pick("仅手动连接小智，不自动重连", "Manual connect only, no auto reconnect");
}

static const char *ui_ai_weather_weather_summary(bool enabled)
{
    if (enabled)
    {
        return ui_i18n_pick("按周期自动更新天气信息", "Refresh weather on schedule");
    }

    return ui_i18n_pick("关闭后台自动天气刷新", "Disable background weather refresh");
}

static void ui_ai_weather_apply_button_state(lv_obj_t *button, bool selected)
{
    lv_obj_t *label;

    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button,
                              selected ? lv_color_black() : lv_color_white(),
                              0);

    label = lv_obj_get_child(button, 0);
    if (label != NULL)
    {
        lv_obj_set_style_text_color(label,
                                    selected ? lv_color_white() : lv_color_black(),
                                    0);
    }
}

static void ui_ai_weather_refresh(void)
{
    bool ai_auto_resume = app_config_get_ai_auto_resume();
    bool weather_auto_refresh = app_config_get_weather_auto_refresh();
    char line[96];

    if (s_ai_weather_ai_label != NULL)
    {
        rt_snprintf(line,
                    sizeof(line),
                    "%s\n%s",
                    ui_ai_weather_ai_title(),
                    ui_ai_weather_ai_summary(ai_auto_resume));
        lv_label_set_text(s_ai_weather_ai_label, line);
    }

    if (s_ai_weather_weather_label != NULL)
    {
        rt_snprintf(line,
                    sizeof(line),
                    "%s\n%s",
                    ui_ai_weather_weather_title(),
                    ui_ai_weather_weather_summary(weather_auto_refresh));
        lv_label_set_text(s_ai_weather_weather_label, line);
    }

    ui_ai_weather_apply_button_state(s_ai_weather_ai_on, ai_auto_resume);
    ui_ai_weather_apply_button_state(s_ai_weather_ai_off, !ai_auto_resume);
    ui_ai_weather_apply_button_state(s_ai_weather_weather_on, weather_auto_refresh);
    ui_ai_weather_apply_button_state(s_ai_weather_weather_off, !weather_auto_refresh);
}

static void ui_ai_weather_toggle_event_cb(lv_event_t *e)
{
    uintptr_t user_data;
    ai_weather_toggle_kind_t kind;
    bool enabled;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    user_data = (uintptr_t)lv_event_get_user_data(e);
    kind = (ai_weather_toggle_kind_t)((user_data >> 1) & 0x1U);
    enabled = (user_data & 0x1U) ? true : false;

    if (kind == AI_WEATHER_TOGGLE_AI_AUTO_RESUME)
    {
        app_config_set_ai_auto_resume(enabled);
    }
    else
    {
        app_config_set_weather_auto_refresh(enabled);
    }

    (void)app_config_save();
    ui_ai_weather_refresh();
}

void ui_AI_Weather_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_AI_Weather_Settings != NULL)
    {
        return;
    }

    ui_AI_Weather_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page,
                             ui_AI_Weather_Settings,
                             ui_ai_weather_title_text(),
                             UI_SCREEN_SETTINGS);

    s_ai_weather_ai_label = ui_create_label(page.content,
                                            "",
                                            36,
                                            56,
                                            456,
                                            64,
                                            24,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            true);
    s_ai_weather_weather_label = ui_create_label(page.content,
                                                 "",
                                                 36,
                                                 304,
                                                 456,
                                                 64,
                                                 24,
                                                 LV_TEXT_ALIGN_LEFT,
                                                 false,
                                                 true);

    s_ai_weather_ai_on = ui_create_button(page.content,
                                          36,
                                          156,
                                          216,
                                          88,
                                          ui_ai_weather_toggle_on_text(),
                                          26,
                                          UI_SCREEN_NONE,
                                          false);
    s_ai_weather_ai_off = ui_create_button(page.content,
                                           276,
                                           156,
                                           216,
                                           88,
                                           ui_ai_weather_toggle_off_text(),
                                           26,
                                           UI_SCREEN_NONE,
                                           false);
    s_ai_weather_weather_on = ui_create_button(page.content,
                                               36,
                                               404,
                                               216,
                                               88,
                                               ui_ai_weather_toggle_on_text(),
                                               26,
                                               UI_SCREEN_NONE,
                                               false);
    s_ai_weather_weather_off = ui_create_button(page.content,
                                                276,
                                                404,
                                                216,
                                                88,
                                                ui_ai_weather_toggle_off_text(),
                                                26,
                                                UI_SCREEN_NONE,
                                                false);

    lv_obj_add_event_cb(s_ai_weather_ai_on,
                        ui_ai_weather_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((AI_WEATHER_TOGGLE_AI_AUTO_RESUME << 1) | 1U));
    lv_obj_add_event_cb(s_ai_weather_ai_off,
                        ui_ai_weather_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((AI_WEATHER_TOGGLE_AI_AUTO_RESUME << 1) | 0U));
    lv_obj_add_event_cb(s_ai_weather_weather_on,
                        ui_ai_weather_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((AI_WEATHER_TOGGLE_WEATHER_REFRESH << 1) | 1U));
    lv_obj_add_event_cb(s_ai_weather_weather_off,
                        ui_ai_weather_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((AI_WEATHER_TOGGLE_WEATHER_REFRESH << 1) | 0U));

    ui_ai_weather_refresh();
}

void ui_AI_Weather_Settings_screen_destroy(void)
{
    s_ai_weather_ai_label = NULL;
    s_ai_weather_weather_label = NULL;
    s_ai_weather_ai_on = NULL;
    s_ai_weather_ai_off = NULL;
    s_ai_weather_weather_on = NULL;
    s_ai_weather_weather_off = NULL;

    if (ui_AI_Weather_Settings != NULL)
    {
        lv_obj_delete(ui_AI_Weather_Settings);
        ui_AI_Weather_Settings = NULL;
    }
}
