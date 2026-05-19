#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "config/app_config.h"
#include "rtthread.h"

#include <stdint.h>

lv_obj_t *ui_Sleep_Time = NULL;

typedef struct
{
    const char *zh;
    const char *en;
    uint32_t timeout_sec;
    int x;
    int y;
} ui_sleep_time_option_t;

static const ui_sleep_time_option_t s_sleep_time_options[] = {
    {"5分钟", "5 min", 5U * 60U, 32, 205},
    {"10分钟", "10 min", 10U * 60U, 286, 204},
    {"15分钟", "15 min", 15U * 60U, 35, 318},
    {"30分钟", "30 min", 30U * 60U, 289, 317},
    {"1小时", "1 h", 60U * 60U, 29, 431},
    {"8小时", "8 h", 8U * 60U * 60U, 283, 430},
    {"12小时", "12 h", 12U * 60U * 60U, 32, 544},
    {"永不", "Never", 0U, 286, 543},
};

static lv_obj_t *ui_sleep_time_plain_obj(lv_obj_t *parent,
                                         int x,
                                         int y,
                                         int w,
                                         int h,
                                         int radius,
                                         lv_opa_t opa,
                                         uint32_t bg,
                                         int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_sleep_time_option_event_cb(lv_event_t *e)
{
    const ui_sleep_time_option_t *option;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    option = (const ui_sleep_time_option_t *)lv_event_get_user_data(e);
    if (option == NULL)
    {
        return;
    }

    app_config_set_display_standby_timeout_sec(option->timeout_sec);
    (void)app_config_save();
    ui_Settings_screen_destroy();
    ui_runtime_switch_to(UI_SCREEN_SETTINGS);
}

static void ui_sleep_time_create_option(lv_obj_t *parent, const ui_sleep_time_option_t *option)
{
    lv_obj_t *button;

    button = ui_sleep_time_plain_obj(parent, option->x, option->y, 206, 88, 32, LV_OPA_COVER, 0xffffff, 1);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, ui_sleep_time_option_event_cb, LV_EVENT_CLICKED, (void *)option);

    ui_create_label(button,
                    ui_i18n_pick(option->zh, option->en),
                    0,
                    28,
                    206,
                    31,
                    26,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

void ui_Sleep_Time_screen_init(void)
{
    size_t i;

    if (ui_Sleep_Time != NULL)
    {
        return;
    }

    ui_Sleep_Time = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Sleep_Time, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Sleep_Time, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Sleep_Time, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_Sleep_Time, ui_i18n_pick("休眠时间", "Sleep Time"), UI_SCREEN_SETTINGS);

    ui_create_label(ui_Sleep_Time,
                    ui_i18n_pick("请输入自定义时间", "Set a custom time"),
                    32,
                    120,
                    300,
                    31,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    for (i = 0; i < sizeof(s_sleep_time_options) / sizeof(s_sleep_time_options[0]); ++i)
    {
        ui_sleep_time_create_option(ui_Sleep_Time, &s_sleep_time_options[i]);
    }
}

void ui_Sleep_Time_screen_destroy(void)
{
    if (ui_Sleep_Time != NULL)
    {
        lv_obj_delete(ui_Sleep_Time);
        ui_Sleep_Time = NULL;
    }
}
