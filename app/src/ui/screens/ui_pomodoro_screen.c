#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

typedef enum
{
    POMODORO_ACTION_PRIMARY = 0,
    POMODORO_ACTION_RESET = 1,
} pomodoro_action_t;

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *status_label;
    lv_obj_t *badge_label;
    lv_obj_t *time_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *hint_label;
    lv_obj_t *primary_button;
    lv_obj_t *primary_button_label;
    lv_obj_t *reset_button;
    lv_obj_t *reset_button_label;
    lv_timer_t *timer;
} pomodoro_ui_t;

extern const lv_image_dsc_t pomodoro_back;
extern const lv_image_dsc_t pomodoro_home;
extern const lv_image_dsc_t pomodoro_mascot;
extern const lv_image_dsc_t pomodoro_bulb;
extern const lv_image_dsc_t pomodoro_play;
extern const lv_image_dsc_t pomodoro_reset;

lv_obj_t *ui_Pomodoro = NULL;

static xiaozhi_home_screen_refs_t s_pomodoro_status_refs;
static pomodoro_ui_t s_pomodoro_ui = {0};
static bool s_pomodoro_running = false;
static bool s_pomodoro_completed = false;
static uint32_t s_pomodoro_remaining_ms = 25U * 60U * 1000U;
static uint32_t s_pomodoro_run_started_tick = 0U;
static uint32_t s_pomodoro_run_start_remaining_ms = 25U * 60U * 1000U;

static void pomodoro_timer_cb(lv_timer_t *timer);
static void pomodoro_action_event_cb(lv_event_t *e);

static void pomodoro_style_box(lv_obj_t *obj, bool filled, int radius, int border_width)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, filled ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, ui_px_x(border_width), 0);
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *pomodoro_create_box(lv_obj_t *parent,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     int radius,
                                     int border_width,
                                     bool filled)
{
    lv_obj_t *obj = lv_obj_create(parent);
    pomodoro_style_box(obj, filled, radius, border_width);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    return obj;
}

static lv_obj_t *pomodoro_create_nav_hit_button(lv_obj_t *parent,
                                                 int x,
                                                 int y,
                                                 int w,
                                                 int h,
                                                 ui_screen_id_t target)
{
    lv_obj_t *button = lv_button_create(parent);

    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    ui_attach_nav_event(button, target);
    return button;
}

static void pomodoro_add_divider(lv_obj_t *parent, int y)
{
    lv_obj_t *line = pomodoro_create_box(parent, 18, y, 492, 2, 0, 0, true);
    lv_obj_set_style_border_width(line, 0, 0);
}

static const char *pomodoro_primary_text(void)
{
    if (s_pomodoro_running)
    {
        return ui_i18n_pick("暂停专注", "Pause");
    }

    if (s_pomodoro_completed)
    {
        return ui_i18n_pick("重新开始", "Restart");
    }

    if (s_pomodoro_remaining_ms < 25U * 60U * 1000U)
    {
        return ui_i18n_pick("继续专注", "Resume");
    }

    return ui_i18n_pick("开始专注", "Start Focus");
}

static const char *pomodoro_badge_text(void)
{
    if (s_pomodoro_running)
    {
        return ui_i18n_pick("专注中", "Focused");
    }

    if (s_pomodoro_completed)
    {
        return ui_i18n_pick("已完成", "Done");
    }

    if (s_pomodoro_remaining_ms < 25U * 60U * 1000U)
    {
        return ui_i18n_pick("已暂停", "Paused");
    }

    return ui_i18n_pick("未开始", "Ready");
}

static const char *pomodoro_status_text(void)
{
    if (s_pomodoro_running)
    {
        return ui_i18n_pick("状态：专注中", "Status: Focused");
    }

    if (s_pomodoro_completed)
    {
        return ui_i18n_pick("状态：已完成", "Status: Completed");
    }

    if (s_pomodoro_remaining_ms < 25U * 60U * 1000U)
    {
        return ui_i18n_pick("状态：已暂停", "Status: Paused");
    }

    return ui_i18n_pick("状态：未开始", "Status: Ready");
}

static void pomodoro_sync_remaining(void)
{
    if (!s_pomodoro_running)
    {
        return;
    }

    uint32_t elapsed = lv_tick_elaps(s_pomodoro_run_started_tick);
    if (elapsed >= s_pomodoro_run_start_remaining_ms)
    {
        s_pomodoro_remaining_ms = 0U;
        s_pomodoro_running = false;
        s_pomodoro_completed = true;
        return;
    }

    s_pomodoro_remaining_ms = s_pomodoro_run_start_remaining_ms - elapsed;
}

static void pomodoro_update_timer_state(void)
{
    if (s_pomodoro_ui.screen == NULL)
    {
        return;
    }

    if (s_pomodoro_running)
    {
        if (s_pomodoro_ui.timer == NULL)
        {
            s_pomodoro_ui.timer = lv_timer_create(pomodoro_timer_cb, 1000, NULL);
        }
    }
    else if (s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }
}

static void pomodoro_render_time(void)
{
    uint32_t remaining = s_pomodoro_remaining_ms;
    uint32_t minutes = remaining / 60000U;
    uint32_t seconds = (remaining / 1000U) % 60U;

    if (s_pomodoro_ui.time_label != NULL)
    {
        lv_label_set_text_fmt(s_pomodoro_ui.time_label, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
    }
}

static void pomodoro_refresh_ui(void)
{
    pomodoro_sync_remaining();

    if (s_pomodoro_ui.badge_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.badge_label, pomodoro_badge_text());
    }

    if (s_pomodoro_ui.status_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.status_label, pomodoro_status_text());
    }

    if (s_pomodoro_ui.primary_button_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.primary_button_label, pomodoro_primary_text());
    }

    if (s_pomodoro_ui.reset_button_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.reset_button_label, ui_i18n_pick("重置", "Reset"));
    }

    if (s_pomodoro_ui.subtitle_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.subtitle_label, ui_i18n_pick("专注当下  ·  提升效率", "Focus now  ·  Improve efficiency"));
    }

    if (s_pomodoro_ui.hint_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.hint_label,
                          ui_i18n_pick("专注期间请远离干扰\n完成后可获得成长奖励，助力成长！",
                                       "Stay away from distractions\nFinish to earn growth rewards."));
    }

    pomodoro_render_time();
}

static void pomodoro_set_running(bool running)
{
    if (running)
    {
        if (s_pomodoro_remaining_ms == 0U || s_pomodoro_completed)
        {
            s_pomodoro_remaining_ms = 25U * 60U * 1000U;
            s_pomodoro_completed = false;
        }

        s_pomodoro_run_started_tick = lv_tick_get();
        s_pomodoro_run_start_remaining_ms = s_pomodoro_remaining_ms;
        s_pomodoro_running = true;
    }
    else
    {
        pomodoro_sync_remaining();
        s_pomodoro_running = false;
    }

    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

static void pomodoro_reset_session(void)
{
    s_pomodoro_running = false;
    s_pomodoro_completed = false;
    s_pomodoro_remaining_ms = 25U * 60U * 1000U;
    s_pomodoro_run_started_tick = 0U;
    s_pomodoro_run_start_remaining_ms = 25U * 60U * 1000U;
    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

static lv_obj_t *pomodoro_create_action_button(lv_obj_t *parent,
                                               int x,
                                               int y,
                                               int w,
                                               int h,
                                               bool filled,
                                               pomodoro_action_t action,
                                               const lv_image_dsc_t *icon,
                                               int icon_x,
                                               int icon_y,
                                               int icon_w,
                                               int icon_h,
                                               int label_x,
                                               int label_w,
                                               int font_size,
                                               lv_obj_t **out_label)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *img;
    lv_obj_t *label;

    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    pomodoro_style_box(button, filled, 12, 2);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, pomodoro_action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    img = ui_create_image_slot(button, icon_x, icon_y, icon_w, icon_h);
    ui_img_set_src(img, icon);
    if (filled)
    {
        lv_obj_set_style_img_recolor(img, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    }

    label = ui_create_label(button,
                            "",
                            label_x,
                            15,
                            label_w,
                            35,
                            font_size,
                            LV_TEXT_ALIGN_LEFT,
                            false,
                            false);
    lv_obj_set_style_text_color(label, filled ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);

    if (out_label != NULL)
    {
        *out_label = label;
    }

    return button;
}

static void pomodoro_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    pomodoro_refresh_ui();

    if (!s_pomodoro_running && s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }
}

static void pomodoro_action_event_cb(lv_event_t *e)
{
    pomodoro_action_t action = (pomodoro_action_t)(uintptr_t)lv_event_get_user_data(e);

    if (action == POMODORO_ACTION_PRIMARY)
    {
        pomodoro_set_running(!s_pomodoro_running);
    }
    else if (action == POMODORO_ACTION_RESET)
    {
        pomodoro_reset_session();
    }
}

void ui_Pomodoro_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *badge;
    lv_obj_t *dot;
    lv_obj_t *hint_box;
    lv_obj_t *img;
    lv_obj_t *parent;

    if (ui_Pomodoro != NULL)
    {
        return;
    }

    ui_Pomodoro = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Pomodoro, ui_i18n_pick("番茄时间", "Pomodoro"), UI_SCREEN_TIME_MANAGE);
    parent = page.content;

    badge = pomodoro_create_box(parent, 203, 21, 122, 37, 19, 2, false);
    dot = pomodoro_create_box(badge, 19, 12, 13, 13, 7, 0, true);
    lv_obj_set_style_border_width(dot, 0, 0);
    s_pomodoro_ui.badge_label = ui_create_label(badge, "", 46, 6, 62, 24, 24, LV_TEXT_ALIGN_LEFT, false, false);

    s_pomodoro_ui.subtitle_label = ui_create_label(parent,
                                                   "",
                                                   0,
                                                   82,
                                                   528,
                                                   32,
                                                   27,
                                                   LV_TEXT_ALIGN_CENTER,
                                                   false,
                                                   false);

    s_pomodoro_ui.time_label = ui_create_label(parent,
                                               "25:00",
                                               0,
                                               142,
                                               528,
                                               88,
                                               74,
                                               LV_TEXT_ALIGN_CENTER,
                                               false,
                                               false);

    img = ui_create_image_slot(parent, 196, 242, 136, 158);
    ui_img_set_src(img, &pomodoro_mascot);

    s_pomodoro_ui.status_label = ui_create_label(parent,
                                                 "",
                                                 0,
                                                 411,
                                                 528,
                                                 32,
                                                 26,
                                                 LV_TEXT_ALIGN_CENTER,
                                                 false,
                                                 false);

    hint_box = pomodoro_create_box(parent, 63, 443, 402, 100, 11, 2, false);
    img = ui_create_image_slot(hint_box, 22, 22, 58, 64);
    ui_img_set_src(img, &pomodoro_bulb);
    ui_create_label(hint_box, ui_i18n_pick("小提示", "Tips"), 103, 25, 220, 34, 28, LV_TEXT_ALIGN_LEFT, false, false);
    s_pomodoro_ui.hint_label = ui_create_label(hint_box, "", 103, 63, 280, 34, 20, LV_TEXT_ALIGN_LEFT, false, true);
    lv_obj_set_style_text_line_space(s_pomodoro_ui.hint_label, 8, 0);

    s_pomodoro_ui.primary_button = pomodoro_create_action_button(parent,
                                                                 58,
                                                                 562,
                                                                 223,
                                                                 62,
                                                                 true,
                                                                 POMODORO_ACTION_PRIMARY,
                                                                 &pomodoro_play,
                                                                 42,
                                                                 15,
                                                                 28,
                                                                 34,
                                                                 83,
                                                                 130,
                                                                 31,
                                                                 &s_pomodoro_ui.primary_button_label);
    s_pomodoro_ui.reset_button = pomodoro_create_action_button(parent,
                                                              303,
                                                              562,
                                                              167,
                                                              62,
                                                              false,
                                                              POMODORO_ACTION_RESET,
                                                              &pomodoro_reset,
                                                              39,
                                                              14,
                                                              34,
                                                              34,
                                                              84,
                                                              70,
                                                              31,
                                                              &s_pomodoro_ui.reset_button_label);

    s_pomodoro_ui.screen = ui_Pomodoro;
    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

void ui_Pomodoro_screen_destroy(void)
{
    if (s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }

    memset(&s_pomodoro_ui, 0, sizeof(s_pomodoro_ui));
    memset(&s_pomodoro_status_refs, 0, sizeof(s_pomodoro_status_refs));

    if (ui_Pomodoro != NULL)
    {
        lv_obj_delete(ui_Pomodoro);
        ui_Pomodoro = NULL;
    }
}
