#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "../../xiaozhi/weather/weather.h"

lv_obj_t *ui_Weather_Toggle = NULL;

static lv_obj_t *s_weather_toggle_button = NULL;
static lv_obj_t *s_weather_toggle_desc = NULL;

static lv_obj_t *create_toggle_divider(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *line = lv_obj_create(parent);

    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(line, ui_px_w(w), 1);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_shadow_width(line, 0, 0);
    lv_obj_set_style_outline_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xD9D9D9), 0);
    return line;
}

static void ui_weather_toggle_refresh(void)
{
    bool enabled = xiaozhi_weather_is_home_entry_enabled();

    if (s_weather_toggle_button != NULL)
    {
        lv_obj_t *label = lv_obj_get_child(s_weather_toggle_button, 0);

        if (label != NULL)
        {
            lv_label_set_text(label, enabled ? ui_i18n_pick("开启", "On") : ui_i18n_pick("关闭", "Off"));
        }
    }

    if (s_weather_toggle_desc != NULL)
    {
        lv_label_set_text(s_weather_toggle_desc,
                          enabled ?
                              ui_i18n_pick("关闭后首页不显示天气入口，但天气功能仍可从设置页进入。",
                                           "Hidden on Home, but Weather stays available from Settings.") :
                              ui_i18n_pick("开启后首页会恢复天气入口，并继续自动同步天气数据。",
                                           "Shown on Home again, and weather data keeps syncing automatically."));
    }
}

static void ui_weather_toggle_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    xiaozhi_weather_set_home_entry_enabled(!xiaozhi_weather_is_home_entry_enabled());
    ui_weather_toggle_refresh();

    ui_Home_screen_destroy();
    ui_Settings_screen_destroy();
    ui_runtime_reload(UI_SCREEN_WEATHER_TOGGLE);
}

void ui_Weather_Toggle_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Weather_Toggle != NULL)
    {
        return;
    }

    ui_Weather_Toggle = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Weather_Toggle, ui_i18n_pick("天气开关", "Weather Entry"), UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    ui_i18n_pick("首页显示天气", "Show on Home"),
                    48,
                    92,
                    180,
                    42,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_weather_toggle_button = ui_create_button(panel, 352, 92, 88, 40, ui_i18n_pick("开启", "On"), 18, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(s_weather_toggle_button, ui_weather_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    create_toggle_divider(panel, 44, 146, 440);
    s_weather_toggle_desc = ui_create_label(panel,
                                            "",
                                            48,
                                            160,
                                            440,
                                            58,
                                            18,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            true);
    ui_weather_toggle_refresh();
}

void ui_Weather_Toggle_screen_destroy(void)
{
    if (ui_Weather_Toggle != NULL)
    {
        lv_obj_delete(ui_Weather_Toggle);
        ui_Weather_Toggle = NULL;
    }

    s_weather_toggle_button = NULL;
    s_weather_toggle_desc = NULL;
}
