#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "config/app_config.h"
#include "rtthread.h"

lv_obj_t *ui_Brightness = NULL;

static lv_obj_t *s_brightness_value_label = NULL;
static lv_obj_t *s_brightness_fill = NULL;

extern void app_set_panel_brightness(rt_uint8_t brightness);
extern rt_uint8_t app_get_panel_brightness(void);

static const char *ui_brightness_screen_title(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Brightness";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "屏幕亮度";
    }
}

static const char *ui_brightness_current_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Current Level";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "当前亮度";
    }
}

static const char *ui_brightness_decrease_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Dim Screen";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "降低亮度";
    }
}

static const char *ui_brightness_increase_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Brighten";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "提高亮度";
    }
}

static const char *ui_brightness_hint_text(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "If brightness is off, raising it again resumes from 50%.";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "关闭后保持熄屏，再次提高亮度会从 50% 恢复。";
    }
}

static lv_obj_t *create_brightness_track(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *track = lv_obj_create(parent);

    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(track, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(track, ui_px_w(w), ui_px_h(18));
    lv_obj_set_style_radius(track, ui_px_y(9), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(track, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(track, 2, 0);
    lv_obj_set_style_shadow_width(track, 0, 0);
    lv_obj_set_style_outline_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    return track;
}

static lv_obj_t *create_brightness_fill(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *fill = lv_obj_create(parent);

    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(fill, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(fill, ui_px_w(w), ui_px_h(10));
    lv_obj_set_style_radius(fill, ui_px_y(5), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_shadow_width(fill, 0, 0);
    lv_obj_set_style_outline_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    return fill;
}

static rt_uint8_t ui_brightness_step_down(rt_uint8_t brightness)
{
    if (brightness == 0U)
    {
        return 0U;
    }
    if (brightness <= 10U)
    {
        return 0U;
    }
    return (rt_uint8_t)(brightness - 10U);
}

static rt_uint8_t ui_brightness_step_up(rt_uint8_t brightness)
{
    if (brightness == 0U)
    {
        return 10U;
    }
    if (brightness >= 100U)
    {
        return 100U;
    }
    return (rt_uint8_t)(brightness + 10U);
}

static void ui_brightness_refresh(void)
{
    rt_uint8_t brightness = app_get_panel_brightness();
    int fill_width = 0;
    char value_text[24];

    if (brightness > 100U)
    {
        brightness = 100U;
    }

    fill_width = (440 * (int)brightness) / 100;

    if (s_brightness_fill != NULL)
    {
        lv_obj_set_width(s_brightness_fill, ui_px_w(fill_width));
    }

    if (s_brightness_value_label != NULL)
    {
        if (brightness == 0U)
        {
            switch (ui_settings_get_language())
            {
            case UI_SETTINGS_LANGUAGE_EN_US:
                rt_snprintf(value_text, sizeof(value_text), "Off");
                break;
            case UI_SETTINGS_LANGUAGE_ZH_CN:
            default:
                rt_snprintf(value_text, sizeof(value_text), "已关闭");
                break;
            }
        }
        else
        {
            rt_snprintf(value_text, sizeof(value_text), "%u%%", (unsigned int)brightness);
        }
        lv_label_set_text(s_brightness_value_label, value_text);
    }
}

static void ui_brightness_adjust_event_cb(lv_event_t *e)
{
    intptr_t delta;
    rt_uint8_t brightness;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    delta = (intptr_t)lv_event_get_user_data(e);
    brightness = app_get_panel_brightness();
    brightness = (delta < 0) ? ui_brightness_step_down(brightness) : ui_brightness_step_up(brightness);
    app_set_panel_brightness(brightness);
    app_config_set_display_brightness(brightness);
    app_config_save();
    ui_Settings_screen_destroy();
    ui_brightness_refresh();
    if (ui_runtime_get_active_screen_id() == UI_SCREEN_BRIGHTNESS)
    {
        lv_obj_invalidate(ui_Brightness);
    }
}

void ui_Brightness_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;
    lv_obj_t *button;

    if (ui_Brightness != NULL)
    {
        return;
    }

    ui_Brightness = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Brightness, ui_brightness_screen_title(), UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    ui_brightness_current_label(),
                    48,
                    92,
                    140,
                    42,
                    22,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_brightness_value_label = ui_create_label(panel,
                                               "",
                                               336,
                                               92,
                                               138,
                                               42,
                                               26,
                                               LV_TEXT_ALIGN_RIGHT,
                                               false,
                                               false);

    create_brightness_track(panel, 44, 162, 440);
    s_brightness_fill = create_brightness_fill(panel, 48, 166, 0);

    button = ui_create_button(panel, 44, 224, 204, 50, ui_brightness_decrease_label(), 22, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(button, ui_brightness_adjust_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    button = ui_create_button(panel, 280, 224, 204, 50, ui_brightness_increase_label(), 22, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(button, ui_brightness_adjust_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    ui_create_label(panel,
                    ui_brightness_hint_text(),
                    44,
                    304,
                    440,
                    52,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);
    ui_brightness_refresh();
}

void ui_Brightness_screen_destroy(void)
{
    if (ui_Brightness != NULL)
    {
        lv_obj_delete(ui_Brightness);
        ui_Brightness = NULL;
    }

    s_brightness_value_label = NULL;
    s_brightness_fill = NULL;
}
