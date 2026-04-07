#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "rtthread.h"

lv_obj_t *ui_Sleep_Time = NULL;

static lv_obj_t *s_sleep_time_value_label = NULL;
static lv_obj_t *s_sleep_time_desc_label = NULL;

static const char *ui_sleep_time_screen_title(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Sleep Timer";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "助眠时间";
    }
}

static const char *ui_sleep_time_start_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Start Time";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "开始时间";
    }
}

static const char *ui_sleep_time_earlier_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "10 Min Earlier";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "提前 10 分钟";
    }
}

static const char *ui_sleep_time_later_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "10 Min Later";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "延后 10 分钟";
    }
}

static lv_obj_t *create_divider(lv_obj_t *parent, int x, int y, int w)
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

static void ui_sleep_time_refresh(void)
{
    char time_text[16];
    char detail_text[96];
    int minutes = ui_settings_get_sleep_start_minutes();
    int reminder_minutes;

    reminder_minutes = minutes - 20;
    while (reminder_minutes < 0)
    {
        reminder_minutes += 24 * 60;
    }

    if (s_sleep_time_value_label != NULL)
    {
        rt_snprintf(time_text,
                    sizeof(time_text),
                    "%02d:%02d",
                    minutes / 60,
                    minutes % 60);
        lv_label_set_text(s_sleep_time_value_label, time_text);
    }

    if (s_sleep_time_desc_label != NULL)
    {
        switch (ui_settings_get_language())
        {
        case UI_SETTINGS_LANGUAGE_EN_US:
            rt_snprintf(detail_text,
                        sizeof(detail_text),
                        "The device becomes quieter and dimmer at night. Pre-alert starts at %02d:%02d.",
                        reminder_minutes / 60,
                        reminder_minutes % 60);
            break;
        case UI_SETTINGS_LANGUAGE_ZH_CN:
        default:
            rt_snprintf(detail_text,
                        sizeof(detail_text),
                        "夜间提醒将在设定时间后降低操作噪音与亮度，%02d:%02d 开始预提醒。",
                        reminder_minutes / 60,
                        reminder_minutes % 60);
            break;
        }
        lv_label_set_text(s_sleep_time_desc_label, detail_text);
    }
}

static void ui_sleep_time_adjust_event_cb(lv_event_t *e)
{
    intptr_t delta;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    delta = (intptr_t)lv_event_get_user_data(e);
    ui_settings_adjust_sleep_start_minutes((int)delta);
    ui_Settings_screen_destroy();
    ui_sleep_time_refresh();
    if (ui_runtime_get_active_screen_id() == UI_SCREEN_SLEEP_TIME)
    {
        lv_obj_invalidate(ui_Sleep_Time);
    }
}

void ui_Sleep_Time_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;
    lv_obj_t *button;

    if (ui_Sleep_Time != NULL)
    {
        return;
    }

    ui_Sleep_Time = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Sleep_Time, ui_sleep_time_screen_title(), UI_SCREEN_SETTINGS);

    panel = ui_create_card(page.content, 0, 0, 528, 653, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    s_sleep_time_desc_label = ui_create_label(panel,
                                              "",
                                              48,
                                              92,
                                              432,
                                              48,
                                              18,
                                              LV_TEXT_ALIGN_LEFT,
                                              false,
                                              true);
    ui_create_label(panel,
                    ui_sleep_time_start_label(),
                    48,
                    141,
                    120,
                    42,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_sleep_time_value_label = ui_create_label(panel,
                                               "",
                                               378,
                                               141,
                                               102,
                                               42,
                                               24,
                                               LV_TEXT_ALIGN_RIGHT,
                                               false,
                                               false);
    create_divider(panel, 44, 194, 440);

    button = ui_create_button(panel, 44, 209, 440, 50, ui_sleep_time_earlier_label(), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(button, ui_sleep_time_adjust_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-10);
    button = ui_create_button(panel, 44, 269, 440, 50, ui_sleep_time_later_label(), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(button, ui_sleep_time_adjust_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)10);
    ui_sleep_time_refresh();
}

void ui_Sleep_Time_screen_destroy(void)
{
    if (ui_Sleep_Time != NULL)
    {
        lv_obj_delete(ui_Sleep_Time);
        ui_Sleep_Time = NULL;
    }

    s_sleep_time_value_label = NULL;
    s_sleep_time_desc_label = NULL;
}
