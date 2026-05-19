#include "ui.h"
#include "ui_components.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include <string.h>

lv_obj_t *ui_Language = NULL;

typedef struct
{
    ui_settings_language_t language;
    const char *label;
    bool supported;
} ui_language_option_t;

static const ui_language_option_t s_language_options[] = {
    {UI_SETTINGS_LANGUAGE_ZH_CN, "简体中文", true},
    {UI_SETTINGS_LANGUAGE_EN_US, "English（United Kingdom）", true},
};

static const char *ui_language_screen_title(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Language";
    case UI_SETTINGS_LANGUAGE_ZH_TW:
        return "語言";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "语言";
    }
}

static void ui_language_invalidate_all_screens(void)
{
    ui_Home_screen_destroy();
    ui_Standby_screen_destroy();
    ui_Reading_List_screen_destroy();
    ui_Reading_Detail_screen_destroy();
    ui_Pet_screen_destroy();
    ui_Pet_Rules_screen_destroy();
    ui_AI_Dou_screen_destroy();
    ui_Pomodoro_screen_destroy();
    ui_Datetime_screen_destroy();
    ui_Weather_screen_destroy();
    ui_Calendar_screen_destroy();
    ui_Status_Detail_screen_destroy();
    ui_About_screen_destroy();
    ui_Recorder_screen_destroy();
    ui_Record_List_screen_destroy();
    ui_Music_List_screen_destroy();
    ui_Music_Player_screen_destroy();
    ui_Settings_screen_destroy();
    ui_Brightness_screen_destroy();
    ui_Wallpaper_screen_destroy();
    ui_AI_Weather_Settings_screen_destroy();
}

static void ui_language_select_event_cb(lv_event_t *e)
{
    ui_settings_language_t language;
    const ui_language_option_t *option;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    option = (const ui_language_option_t *)lv_event_get_user_data(e);
    if (option == NULL || !option->supported)
    {
        return;
    }

    language = option->language;
    ui_settings_set_language(language);
    ui_language_invalidate_all_screens();
    ui_runtime_reload(UI_SCREEN_LANGUAGE);
}

static lv_obj_t *ui_language_plain_obj(lv_obj_t *parent,
                                       int x,
                                       int y,
                                       int w,
                                       int h,
                                       lv_opa_t opa,
                                       uint32_t bg,
                                       int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_language_create_radio(lv_obj_t *parent, int x, int y, bool selected)
{
    static const lv_point_precise_t check_points[] = {
        {6, 14},
        {12, 20},
        {23, 8},
    };
    lv_obj_t *circle;

    circle = ui_language_plain_obj(parent, x, y, 30, 30, LV_OPA_TRANSP, 0xffffff, 3);
    lv_obj_set_style_radius(circle, ui_px_x(15), 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

    if (selected)
    {
        lv_obj_t *check = lv_line_create(circle);
        lv_line_set_points(check, check_points, sizeof(check_points) / sizeof(check_points[0]));
        lv_obj_set_style_line_color(check, lv_color_hex(0x343434), 0);
        lv_obj_set_style_line_width(check, 3, 0);
        lv_obj_set_style_line_rounded(check, true, 0);
        lv_obj_clear_flag(check, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void ui_language_create_option_row(lv_obj_t *parent,
                                          int y,
                                          const ui_language_option_t *option,
                                          bool selected)
{
    lv_obj_t *row;

    row = ui_language_plain_obj(parent, 0, y, 528, 88, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, (void *)option);
    lv_obj_add_event_cb(row, ui_language_select_event_cb, LV_EVENT_CLICKED, (void *)option);

    ui_create_label(row,
                    option->label,
                    32,
                    28,
                    380,
                    31,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    ui_language_create_radio(row, 462, 16, selected);
}

void ui_Language_screen_init(void)
{
    size_t i;
    ui_settings_language_t selected_language = ui_settings_get_language();

    if (ui_Language != NULL)
    {
        return;
    }

    ui_Language = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Language, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Language, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Language, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_Language, ui_language_screen_title(), UI_SCREEN_SETTINGS);

    for (i = 0; i < sizeof(s_language_options) / sizeof(s_language_options[0]); ++i)
    {
        bool is_selected = s_language_options[i].supported && (s_language_options[i].language == selected_language);

        ui_language_create_option_row(ui_Language, 91 + (int)i * 89, &s_language_options[i], is_selected);
    }
}

void ui_Language_screen_destroy(void)
{
    if (ui_Language != NULL)
    {
        lv_obj_delete(ui_Language);
        ui_Language = NULL;
    }
}
